//! racit-release: one command that builds every RACiT release asset for a tag, checks each zip
//! from a fresh unzip, and attaches them to a DRAFT GitHub release on the fork.
//!
//!   racit-release --tag v1.0.3 --build 126 [--config FILE] [--only mac|win]
//!                 [--carry-from TAG|ZIP] [--build-notes FILE] [--release-notes FILE]
//!                 [--allow-missing-eu] [--no-smoke] [--no-upload] [--keep-builds]
//!
//! The game can't be built on hosted CI: it is lifted from the player's own decrypted EBOOT,
//! which never goes on GitHub. So this runs on a maintainer's Mac, on demand, inside the
//! machine's job queue. Every path to game data comes from the config file (outside the repo,
//! default ~/.config/racit-release/config.toml; see config.example.toml), never from the tree.
//!
//! Steps (docs/RELEASING.md has the long form):
//!  1. a build tree (a git worktree of the repo) checked out at the tag, runtime pinned and
//!     patched (mac/setup_macos.sh), the Windows sources at setup_windows.sh's pins, inputs
//!     copied in and checked against versions/profiles.json;
//!  2. macOS: scripts/pipeline.sh for 1.00 and for update 1.20 (the lifts and the Mac games);
//!  3. Windows, cross-built with zig 0.16: RaCACiT.exe and RaCACiT_compat.exe for 1.00, for
//!     1.20 (builds\vN-u120) and for the European disc when its EBOOT is configured, each lift
//!     passing scripts/check_lift_hooks.py; the launcher exe;
//!  4. the macOS zip (mac/package_macos.sh) and the Windows zip, laid out as the last official
//!     Windows zip (its Into the Nexus folder and shader caches carried over) and compared with it;
//!  5. smoke tests from a fresh unzip of each zip (muted, offscreen);
//!  6. a draft release on the fork with both zips (never published, never upstream).
use anyhow::{Context, Result, anyhow, bail, ensure};
use serde::Deserialize;
use sha2::{Digest, Sha256};
use std::collections::BTreeSet;
use std::fmt::Write as _;
use std::fs;
use std::io::{Read, Write};
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::time::Instant;

// ---------------------------------------------------------------- config ----

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct Config {
    /// The RACiT git repository (any clone with the upstream remote).
    repo: PathBuf,
    /// Scratch for outputs, logs and smoke runs (local, unsynced).
    work: PathBuf,
    /// Build tree; default <repo>/.worktrees/release-build. The builder owns it.
    tree: Option<PathBuf>,
    #[serde(default = "d_upstream_remote")]
    upstream_remote: String,
    #[serde(default = "d_upstream")]
    upstream: String,
    #[serde(default = "d_fork")]
    fork: String,
    #[serde(default = "d_jobs")]
    jobs: u32,
    #[serde(default = "d_python")]
    python: String,
    /// GiB that must stay free on the work volume before each heavy step.
    #[serde(default = "d_min_free")]
    min_free_gib: u64,
    inputs: Inputs,
    mac: Option<MacCfg>,
    win: Option<WinCfg>,
    #[serde(default)]
    smoke: SmokeCfg,
}
fn d_upstream_remote() -> String { "origin".into() }
fn d_upstream() -> String { "HighDrexler/RACiT".into() }
fn d_fork() -> String { "petralithic/RACiT".into() }
fn d_jobs() -> u32 { 6 }
fn d_python() -> String { "python3".into() }
fn d_min_free() -> u64 { 20 }

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct Inputs {
    /// Decrypted BCUS98124 1.00 EBOOT.ELF.
    us100_elf: PathBuf,
    /// Decrypted BCUS98124 update 1.20 EBOOT.ELF (optional: no 1.20 builds without it).
    us120_elf: Option<PathBuf>,
    /// Decrypted BCES00511 1.00 EBOOT.ELF (optional: --allow-missing-eu to release without).
    eu100_elf: Option<PathBuf>,
    /// Decrypted libsre.prx.
    libsre: PathBuf,
}

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct MacCfg {
    /// Lowest macOS the zip runs on. Default: this Mac's own macOS (releases target the latest).
    min: Option<String>,
    /// MACOS_LIB_PREFIX for mac/package_macos.sh: libraries built for `min`. Default: built once by
    /// mac/build_macos_libs.sh into <work>/macos-libs-<min> and reused (Homebrew's bottles can
    /// need a newer point release than this Mac runs, e.g. SDL3 at 27.2 on 27.0).
    lib_prefix: Option<PathBuf>,
}

/// The lowest macOS the Mac builds target: [mac] min, or this Mac's macOS as MAJOR.0.
fn mac_min(c: &Ctx) -> Result<String> {
    if let Some(m) = c.cfg.mac.as_ref().and_then(|m| m.min.clone()) { return Ok(m); }
    let out = Command::new("sw_vers").arg("-productVersion").output()?;
    ensure!(out.status.success(), "sw_vers -productVersion failed");
    let v = String::from_utf8(out.stdout)?;
    let major = v.trim().split('.').next().unwrap_or("").to_string();
    ensure!(!major.is_empty() && major.chars().all(|ch| ch.is_ascii_digit()), "unexpected macOS version {v:?}");
    Ok(format!("{major}.0"))
}

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct WinCfg {
    /// zig 0.16.x (the repository's pin; 0.17 can't build the game).
    zig: PathBuf,
    /// A bin directory with llvm-ar, llvm-ranlib and llvm-rc (the repo's .cmd wrappers only run on Windows).
    llvm_bin: PathBuf,
}

#[derive(Deserialize, Default)]
#[serde(deny_unknown_fields)]
struct SmokeCfg {
    /// The disc's folder (the one holding PS3_GAME) for tests/mac_user_launch.py.
    game: Option<PathBuf>,
    /// HDD installs (hdd0/game/BCUS98124 of a profile that has played) for 1.00 and 1.20.
    installed_100: Option<PathBuf>,
    installed_120: Option<PathBuf>,
    /// The installed update's folder (PARAM.SFO and USRDIR).
    update: Option<PathBuf>,
    /// wine binary and the prefix to run the Windows launcher's self-test in.
    wine: Option<PathBuf>,
    wineprefix: Option<PathBuf>,
    /// racit-wine (rccit-recomp/rust/racit-wine.rs) and its scratch dir: a 1.00 game run under Wine.
    racit_wine: Option<PathBuf>,
    racit_wine_scratch: Option<PathBuf>,
}

// ------------------------------------------------------------------ args ----

struct Args {
    tag: String,
    /// --ref: build this commit instead of the tag (a preflight; never uploaded).
    git_ref: Option<String>,
    build: u32,
    config: PathBuf,
    mac: bool,
    win: bool,
    carry_from: Option<String>,
    build_notes: Option<PathBuf>,
    release_notes: Option<PathBuf>,
    allow_missing_eu: bool,
    smoke: bool,
    upload: bool,
    keep_builds: bool,
    no_queue_check: bool,
}

const USAGE: &str = "usage: racit-release --tag vX.Y.Z --build N [--ref COMMIT] [--config FILE] [--only mac|win]
       [--carry-from TAG|ZIP] [--build-notes FILE] [--release-notes FILE] [--allow-missing-eu]
       [--no-smoke] [--no-upload] [--keep-builds] [--no-queue-check]
Run it inside the job queue: agentq run --label rccit-rel-build --mem 10 -- racit-release ...";

fn parse_args() -> Result<Args> {
    let home = PathBuf::from(std::env::var("HOME")?);
    let mut a = Args {
        tag: String::new(), git_ref: None, build: 0, config: home.join(".config/racit-release/config.toml"),
        mac: true, win: true, carry_from: None, build_notes: None, release_notes: None,
        allow_missing_eu: false, smoke: true, upload: true, keep_builds: false, no_queue_check: false,
    };
    if let Ok(c) = std::env::var("RACIT_RELEASE_CONFIG") { a.config = c.into(); }
    let mut it = std::env::args().skip(1);
    while let Some(k) = it.next() {
        let mut val = || it.next().ok_or_else(|| anyhow!("{k} needs a value\n{USAGE}"));
        match k.as_str() {
            "--tag" => a.tag = val()?,
            "--ref" => a.git_ref = Some(val()?),
            "--build" => a.build = val()?.parse().context("--build N: a number")?,
            "--config" => a.config = val()?.into(),
            "--only" => match val()?.as_str() {
                "mac" => a.win = false,
                "win" => a.mac = false,
                o => bail!("--only mac|win, not {o}"),
            },
            "--carry-from" => a.carry_from = Some(val()?),
            "--build-notes" => a.build_notes = Some(val()?.into()),
            "--release-notes" => a.release_notes = Some(val()?.into()),
            "--allow-missing-eu" => a.allow_missing_eu = true,
            "--no-smoke" => a.smoke = false,
            "--no-upload" => a.upload = false,
            "--keep-builds" => a.keep_builds = true,
            "--no-queue-check" => a.no_queue_check = true,
            "-h" | "--help" => { println!("{USAGE}"); std::process::exit(0) }
            o => bail!("unknown argument {o}\n{USAGE}"),
        }
    }
    ensure!(a.tag.starts_with('v') && a.tag.len() > 1, "--tag vX.Y.Z is needed\n{USAGE}");
    if a.git_ref.is_some() { a.upload = false; }
    ensure!(a.build > 0, "--build N (the game build number, builds\\vN) is needed\n{USAGE}");
    Ok(a)
}

// --------------------------------------------------------------- helpers ----

struct Ctx {
    cfg: Config,
    args: Args,
    ver: String,
    tree: PathBuf,
    out: PathBuf,
    logs: PathBuf,
    bin: PathBuf,
    dist: PathBuf,
    report: String,
    shippable: bool,
    started: Instant,
}

impl Ctx {
    fn note(&mut self, line: impl AsRef<str>) {
        let line = line.as_ref();
        println!("{line}");
        self.report.push_str(line);
        self.report.push('\n');
    }
    fn step(&mut self, what: &str) {
        let t = self.started.elapsed().as_secs();
        println!("\n== [{:>4} min] {what}", t / 60);
    }
    /// Runs a command with its output in logs/<log>.log; on failure, says where and shows the tail.
    fn run(&self, log: &str, cmd: &mut Command) -> Result<()> {
        let path = self.logs.join(format!("{log}.log"));
        let f = fs::File::create(&path)?;
        let desc = format!("{cmd:?}");
        let t = Instant::now();
        let st = cmd.stdin(Stdio::null()).stdout(f.try_clone()?).stderr(f).status()
            .with_context(|| format!("starting {desc}"))?;
        if !st.success() {
            let tail = fs::read_to_string(&path).unwrap_or_default();
            let tail: Vec<&str> = tail.lines().rev().take(25).collect();
            eprintln!("--- last lines of {}:", path.display());
            for l in tail.iter().rev() { eprintln!("{l}"); }
            bail!("{log} failed ({st}); full log {}", path.display());
        }
        println!("   {log}: ok in {} s", t.elapsed().as_secs());
        Ok(())
    }
    fn free_gib(&self) -> Result<u64> {
        let o = Command::new("df").arg("-k").arg(&self.cfg.work).output()?;
        let s = String::from_utf8_lossy(&o.stdout);
        let kib: u64 = s.lines().nth(1).and_then(|l| l.split_whitespace().nth(3)).and_then(|v| v.parse().ok())
            .ok_or_else(|| anyhow!("df: unreadable output"))?;
        Ok(kib / 1024 / 1024)
    }
    fn need_space(&self, what: &str) -> Result<()> {
        let free = self.free_gib()?;
        ensure!(free >= self.cfg.min_free_gib,
            "{what}: only {free} GiB free on the work volume (floor {} GiB, min_free_gib)", self.cfg.min_free_gib);
        Ok(())
    }
    fn py(&self) -> String { format!("{} -I", self.cfg.python) }
}

fn sh(cmd: &str, args: &[&str]) -> Command {
    let mut c = Command::new(cmd);
    c.args(args);
    c
}

/// stdout of a command that must succeed.
fn out<C: std::borrow::BorrowMut<Command>>(mut cmd: C) -> Result<String> {
    let cmd = cmd.borrow_mut();
    let o = cmd.stdin(Stdio::null()).output().with_context(|| format!("starting {cmd:?}"))?;
    ensure!(o.status.success(), "{cmd:?} failed: {}", String::from_utf8_lossy(&o.stderr).trim());
    Ok(String::from_utf8_lossy(&o.stdout).trim().to_string())
}

fn sha256(path: &Path) -> Result<String> {
    let mut f = fs::File::open(path).with_context(|| format!("open {}", path.display()))?;
    let mut h = Sha256::new();
    let mut buf = vec![0u8; 1 << 20];
    loop {
        let n = f.read(&mut buf)?;
        if n == 0 { break; }
        h.update(&buf[..n]);
    }
    Ok(h.finalize().iter().map(|b| format!("{b:02x}")).collect())
}

/// Copies a file (an APFS clone on macOS), replacing what is there.
fn put(from: &Path, to: &Path) -> Result<()> {
    if let Some(p) = to.parent() { fs::create_dir_all(p)?; }
    let _ = fs::remove_file(to);
    fs::copy(from, to).with_context(|| format!("copy {} -> {}", from.display(), to.display()))?;
    Ok(())
}

fn rm_rf(p: &Path) -> Result<()> {
    if p.symlink_metadata().is_ok() {
        if p.is_dir() { fs::remove_dir_all(p)?; } else { fs::remove_file(p)?; }
    }
    Ok(())
}

fn owner_busy() -> Option<String> {
    let home = std::env::var("HOME").unwrap_or_default();
    let av = Path::new(&home).join(".local/bin/av-busy");
    if av.exists() && Command::new(&av).arg("--quiet").status().map(|s| s.success()).unwrap_or(false) {
        return Some("the owner is on a call (av-busy)".into());
    }
    let o = Command::new("pgrep").args(["-xl", "racit-player|RaCACiTRecomp|play-racit"]).output().ok()?;
    let s = String::from_utf8_lossy(&o.stdout).trim().to_string();
    (!s.is_empty()).then(|| format!("a game is running ({s})"))
}

fn walk(dir: &Path, base: &Path, files: &mut Vec<PathBuf>) -> Result<()> {
    let mut entries: Vec<_> = fs::read_dir(dir)?.collect::<std::io::Result<_>>()?;
    entries.sort_by_key(|e| e.file_name());
    for e in entries {
        let p = e.path();
        files.push(p.strip_prefix(base)?.to_path_buf());
        if e.file_type()?.is_dir() { walk(&p, base, files)?; }
    }
    Ok(())
}

// ---------------------------------------------------------------- tree ------

#[derive(Deserialize)]
struct Profiles { profiles: Vec<Profile> }
#[derive(Deserialize)]
struct Profile { id: String, elf_sha256: String }

fn prepare_tree(c: &mut Ctx) -> Result<String> {
    c.step("build tree");
    let repo = c.cfg.repo.clone();
    let git = |args: &[&str]| { let mut g = sh("git", &["-C"]); g.arg(&repo).args(args); g };
    // The tag as upstream has it (a moved local tag must not win).
    let _ = out(&mut git(&["fetch", "-q", &c.cfg.upstream_remote, "--tags"]));
    let source = c.args.git_ref.clone().unwrap_or_else(|| c.args.tag.clone());
    let commit = out(&mut git(&["rev-parse", &format!("{source}^{{commit}}")]))
        .with_context(|| format!("no tag or commit {source} in {}", repo.display()))?;
    let tree = c.tree.clone();
    if !tree.join(".git").exists() {
        out(git(&["worktree", "add", "-q", "--detach"]).arg(&tree).arg(&commit))?;
    } else {
        let dirty = out(sh("git", &["-C"]).arg(&tree).args(["status", "--porcelain", "--untracked-files=no"]))?;
        ensure!(dirty.is_empty(), "the build tree {} has edits; it belongs to the builder:\n{dirty}", tree.display());
        out(sh("git", &["-C"]).arg(&tree).args(["checkout", "-q", "--detach", &commit]))?;
    }
    c.note(format!("- source: {source} at {}", &commit[..12]));

    // Launcher version must be the tag's: the updater compares them.
    let app = fs::read_to_string(tree.join("launcher/src/app.cpp"))?;
    let lver = app.lines().find_map(|l| l.strip_prefix("static const char* kLauncherVersion = \"")
        .and_then(|r| r.strip_suffix("\";"))).ok_or_else(|| anyhow!("no kLauncherVersion in launcher/src/app.cpp"))?;
    ensure!(lver == c.ver, "launcher/src/app.cpp says {lver}, the tag says {}", c.ver);

    // Inputs, checked against the tag's profiles (a wrong dump stops here, not an hour in).
    let profiles: Profiles = serde_json::from_str(&fs::read_to_string(tree.join("versions/profiles.json"))?)?;
    let want = |id: &str| profiles.profiles.iter().find(|p| p.id == id).map(|p| p.elf_sha256.clone());
    let check = |id: &str, p: &Path| -> Result<()> {
        let got = sha256(p)?;
        let exp = want(id).ok_or_else(|| anyhow!("versions/profiles.json has no {id}"))?;
        ensure!(got == exp, "{} is not {id} (sha256 {got}, expected {exp})", p.display());
        Ok(())
    };
    let i = &c.cfg.inputs;
    check("BCUS98124-1.00", &i.us100_elf)?;
    if let Some(p) = &i.us120_elf { check("BCUS98124-1.20", p)?; }
    if let Some(p) = &i.eu100_elf { check("BCES00511-1.00", p)?; }
    put(&i.us100_elf, &tree.join("input/EBOOT.ELF"))?;
    put(&i.libsre, &tree.join("input/libsre.prx"))?;
    if let Some(p) = &i.eu100_elf {
        put(p, &tree.join("input/BCES00511/EBOOT.ELF"))?;
        put(&i.libsre, &tree.join("input/BCES00511/libsre.prx"))?;
    }

    // Runtime at its pin with this tag's patch. The builder owns this checkout, so when the
    // tree pins another runtime commit (1.0.x on 5244db92, 1.1 on v0.19) setup moves it.
    let setup = fs::read_to_string(tree.join("mac/setup_macos.sh"))?;
    let pin = setup.lines().find_map(|l| l.strip_prefix("COMMIT=")).map(str::trim);
    let head = Command::new("git").args(["-C", "tools/ps3recomp", "rev-parse", "HEAD"])
        .current_dir(&tree).output().ok().filter(|o| o.status.success())
        .map(|o| String::from_utf8_lossy(&o.stdout).trim().to_string());
    let moved = matches!((pin, head.as_deref()), (Some(p), Some(h)) if p != h);
    let setup_args: &[&str] = if moved { &["mac/setup_macos.sh", "--reset"] } else { &["mac/setup_macos.sh"] };
    c.run("setup-macos", sh("bash", setup_args).current_dir(&tree))?;
    if c.args.win { windows_tools(c)?; }
    Ok(commit)
}

/// The Windows build's sources at scripts/setup_windows.sh's pins, and static SDL2 built from them
/// with the cross toolchain (what setup_windows.sh does on a Windows host).
fn windows_tools(c: &mut Ctx) -> Result<()> {
    let tree = c.tree.clone();
    let script = fs::read_to_string(tree.join("scripts/setup_windows.sh"))?;
    let mut sdl_changed = false;
    for l in script.lines() {
        let w: Vec<&str> = l.split_whitespace().collect();
        if w.len() != 4 || w[0] != "pin" || w[1] == "ps3recomp" { continue; }
        let (dir, repo, rev) = (w[1], w[2], w[3].trim_matches('"'));
        let d = tree.join("tools").join(dir);
        let stamp = d.join(".racit-release-pin");
        if fs::read_to_string(&stamp).ok().as_deref() == Some(rev) { continue; }
        rm_rf(&d)?;
        fs::create_dir_all(&d)?;
        let g = |args: &[&str]| { let mut x = sh("git", &["-C"]); x.arg(&d).args(args); x };
        out(&mut g(&["init", "-q"]))?;
        out(&mut g(&["remote", "add", "origin", &format!("https://github.com/{repo}")]))?;
        out(&mut g(&["-c", "core.autocrlf=false", "-c", "core.eol=lf", "fetch", "-q", "--depth", "1", "origin", rev]))?;
        out(&mut g(&["-c", "core.autocrlf=false", "-c", "core.eol=lf", "checkout", "-q", "FETCH_HEAD"]))?;
        fs::write(&stamp, rev)?;
        c.note(format!("- tools/{dir}: {repo} at {rev}"));
        if dir == "SDL2-src" { sdl_changed = true; }
    }
    let lib = tree.join("tools/sdl2-win/lib/libSDL2.a");
    if sdl_changed || !lib.exists() {
        rm_rf(&tree.join("tools/sdl2-win-build"))?;
        rm_rf(&tree.join("tools/sdl2-win"))?;
        let tc = c.out.join("zig-windows-machost.cmake");
        let mut cfg = win_cmd(c, &tree, "cmake");
        cfg.args(["-S", "tools/SDL2-src", "-B", "tools/sdl2-win-build", "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
            "-DSDL_SHARED=OFF", "-DSDL_STATIC=ON", "-DSDL_TEST=OFF"])
            .arg(format!("-DCMAKE_TOOLCHAIN_FILE={}", tc.display()))
            .arg(format!("-DCMAKE_INSTALL_PREFIX={}", tree.join("tools/sdl2-win").display()));
        c.run("sdl2-win-configure", &mut cfg)?;
        let mut b = win_cmd(c, &tree, "cmake");
        b.args(["--build", "tools/sdl2-win-build", "--target", "install"]);
        c.run("sdl2-win-build", &mut b)?;
    }
    Ok(())
}

/// A command with the cross toolchain's environment: RACIT_TREE (the toolchain wrapper includes
/// the tree's own zig-windows.cmake), ZIG_EXE, and zig first on PATH.
fn win_cmd(c: &Ctx, tree: &Path, prog: &str) -> Command {
    let w = c.cfg.win.as_ref().expect("win config");
    let zig_dir = w.zig.parent().unwrap_or(Path::new("/"));
    let mut cmd = Command::new(prog);
    cmd.current_dir(tree).env("RACIT_TREE", tree).env("ZIG_EXE", &w.zig)
        .env("PATH", format!("{}:{}", zig_dir.display(), std::env::var("PATH").unwrap_or_default()))
        .env("PY", c.py()).env("CMAKE_BUILD_PARALLEL_LEVEL", c.cfg.jobs.to_string());
    cmd
}

fn write_machost_toolchain(c: &Ctx) -> Result<()> {
    let w = c.cfg.win.as_ref().ok_or_else(|| anyhow!("no [win] section in the config"))?;
    let ver = out(sh(&w.zig.to_string_lossy(), &["version"]))?;
    ensure!(ver.starts_with("0.16."), "{} is zig {ver}; the game needs 0.16.x (0.17 rejects its patchable-function-entry flags)", w.zig.display());
    for t in ["llvm-ar", "llvm-ranlib", "llvm-rc"] {
        ensure!(w.llvm_bin.join(t).exists(), "no {t} in {}", w.llvm_bin.display());
    }
    let l = w.llvm_bin.display();
    fs::write(c.out.join("zig-windows-machost.cmake"), format!(
        "# Written by racit-release: the repository's zig toolchain, with LLVM's ar/ranlib/rc in place of\n\
         # its .cmd wrappers, which only run on a Windows host.\n\
         include(\"$ENV{{RACIT_TREE}}/windows/toolchain/zig-windows.cmake\")\n\
         set(CMAKE_AR     \"{l}/llvm-ar\")\nset(CMAKE_RANLIB \"{l}/llvm-ranlib\")\nset(CMAKE_RC_COMPILER \"{l}/llvm-rc\")\n"))?;
    Ok(())
}

// ------------------------------------------------------------- builds -------

/// The lifts (and on a Mac build, the Mac games): scripts/pipeline.sh for 1.00 and 1.20.
fn pipelines(c: &mut Ctx) -> Result<()> {
    let tree = c.tree.clone();
    let skip_desktop = !c.args.mac;
    c.need_space("pipeline 1.00")?;
    c.step("pipeline: BCUS98124 1.00");
    let mut p = sh("bash", &["scripts/pipeline.sh"]);
    p.current_dir(&tree).env("PY", c.py()).env("CMAKE_BUILD_PARALLEL_LEVEL", c.cfg.jobs.to_string())
        .env("CMAKE_GENERATOR", "Unix Makefiles");
    if skip_desktop { p.env("SKIP_DESKTOP", "1"); }
    let min = if c.args.mac { Some(mac_min(c)?) } else { None };
    if let Some(m) = &min { p.env("MACOSX_DEPLOYMENT_TARGET", m); }
    c.run("pipeline-100", &mut p)?;
    if c.args.mac { put(&tree.join("game/build/RaCACiTRecomp"), &c.bin.join("mac-100/RaCACiTRecomp"))?; }

    if let Some(u120) = c.cfg.inputs.us120_elf.clone() {
        c.need_space("pipeline 1.20")?;
        c.step("pipeline: BCUS98124 update 1.20");
        let mut p = sh("bash", &["scripts/pipeline.sh"]);
        p.arg(&u120).current_dir(&tree).env("PY", c.py()).env("CMAKE_BUILD_PARALLEL_LEVEL", c.cfg.jobs.to_string())
            .env("CMAKE_GENERATOR", "Unix Makefiles").env("LIBSRE", tree.join("input/libsre.prx"));
        if skip_desktop { p.env("SKIP_DESKTOP", "1"); }
        if let Some(m) = &min { p.env("MACOSX_DEPLOYMENT_TARGET", m); }
        c.run("pipeline-120", &mut p)?;
        if c.args.mac {
            put(&tree.join("out/BCUS98124-1.20/build/RaCACiTRecomp"), &c.bin.join("mac-120/RaCACiTRecomp"))?;
        }
    }
    Ok(())
}

struct WinTarget {
    /// Where cmake runs (the tree, or the European copy build/BCES00511).
    root: PathBuf,
    build_dir: &'static str,
    profile: &'static str,
    elf: PathBuf,
    recomp: PathBuf,
    compat: bool,
    /// Path in the release zip, under RACiT_<ver>/builds/vN.
    dest: String,
}

fn hook_check(c: &mut Ctx, root: &Path, recomp: &Path, profile: &str, log: &str) -> Result<()> {
    // Trees from before the Rust lift tools have the Python checker; later ones have check-lift-hooks.
    let mut chk = if root.join("scripts/check_lift_hooks.py").is_file() {
        sh(&c.cfg.python, &["-I", "-X", "utf8", "scripts/check_lift_hooks.py"])
    } else {
        sh("cargo", &["run", "-q", "--release", "--manifest-path", "rust/racit-lift/Cargo.toml", "--bin", "check-lift-hooks", "--"])
    };
    chk.arg(recomp).args(["--profile", profile]).current_dir(root);
    c.run(log, &mut chk)?;
    let msg = fs::read_to_string(c.logs.join(format!("{log}.log"))).unwrap_or_default();
    c.note(format!("- lift hooks {profile}: PASS ({})", msg.lines().last().unwrap_or("").trim()));
    Ok(())
}

fn build_windows(c: &mut Ctx) -> Result<()> {
    let tree = c.tree.clone();
    let us120 = c.cfg.inputs.us120_elf.clone();
    let mut targets = vec![];
    for compat in [false, true] {
        let exe = if compat { "RaCACiT_compat.exe" } else { "RaCACiT.exe" };
        targets.push(WinTarget { root: tree.clone(), build_dir: if compat { "game/build-win-compat" } else { "game/build-win" },
            profile: "BCUS98124-1.00", elf: tree.join("input/EBOOT.ELF"), recomp: tree.join("game/src/recomp"), compat,
            dest: format!("v{}/{exe}", c.args.build) });
        if let Some(u) = &us120 {
            targets.push(WinTarget { root: tree.clone(),
                build_dir: if compat { "game/build-win-u120-compat" } else { "game/build-win-u120" },
                profile: "BCUS98124-1.20", elf: u.clone(), recomp: tree.join("out/BCUS98124-1.20/recomp"), compat,
                dest: format!("v{}-u120/{exe}", c.args.build) });
        }
    }
    hook_check(c, &tree, &tree.join("game/src/recomp"), "BCUS98124-1.00", "hooks-100")?;
    if us120.is_some() { hook_check(c, &tree, &tree.join("out/BCUS98124-1.20/recomp"), "BCUS98124-1.20", "hooks-120")?; }

    // The European disc: a converted copy of the project (scripts/build_version.sh's steps, with
    // the Mac's python and the cross toolchain).
    if c.cfg.inputs.eu100_elf.is_some() {
        c.need_space("European lift")?;
        c.step("European disc BCES00511: retarget + lift");
        let mut r = sh(&c.cfg.python, &["-I", "scripts/retarget.py", "BCES00511"]);
        r.current_dir(&tree);
        c.run("retarget-eu", &mut r)?;
        let eu = tree.join("build/BCES00511");
        let mut p = sh("bash", &["scripts/pipeline.sh"]);
        p.current_dir(&eu).env("PY", c.py()).env("SKIP_DESKTOP", "1").env("CMAKE_BUILD_PARALLEL_LEVEL", c.cfg.jobs.to_string());
        c.run("pipeline-eu", &mut p)?;
        hook_check(c, &eu, &eu.join("game/src/recomp"), "BCES00511-1.00", "hooks-eu")?;
        for compat in [false, true] {
            targets.push(WinTarget { root: eu.clone(), build_dir: if compat { "game/build-win-compat" } else { "game/build-win" },
                profile: "BCES00511-1.00", elf: tree.join("input/BCES00511/EBOOT.ELF"), recomp: eu.join("game/src/recomp"), compat,
                dest: format!("v{}/RaCACiT_BCES00511{}.exe", c.args.build, if compat { "_compat" } else { "" }) });
        }
    } else if c.args.allow_missing_eu {
        c.shippable = false;
        c.note("- European disc: NOT BUILT (no inputs.eu100_elf; --allow-missing-eu). Not shippable: 1.0.1 had the EU programs.");
    } else {
        bail!("no inputs.eu100_elf in the config: the official Windows zip carries RaCACiT_BCES00511(.exe/_compat.exe). \
               Configure the European EBOOT, or pass --allow-missing-eu for a test build");
    }

    let tc = c.out.join("zig-windows-machost.cmake");
    let python = out(sh("/usr/bin/env", &["which", &c.cfg.python]))?;
    for t in targets {
        c.need_space(&format!("Windows build {}", t.dest))?;
        c.step(&format!("Windows {} {}", t.profile, if t.compat { "compat" } else { "AVX2" }));
        let log = t.dest.replace(['/', '.'], "-");
        let mut cfg = win_cmd(c, &t.root, "cmake");
        cfg.args(["-S", "windows", "-B", t.build_dir, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release"])
            .arg(format!("-DCMAKE_TOOLCHAIN_FILE={}", tc.display()))
            .arg(format!("-DPython3_EXECUTABLE={python}"))
            .arg(format!("-DRACACIT_GAME_PROFILE={}", t.profile))
            .arg(format!("-DRACACIT_ELF={}", t.elf.display()))
            .arg(format!("-DRACACIT_CPU_COMPAT={}", if t.compat { "ON" } else { "OFF" }));
        if t.root != tree {
            cfg.arg(format!("-DSDL2_DIR={}", tree.join("tools/sdl2-win/lib/cmake/SDL2").display()));
        }
        c.run(&format!("{log}-configure"), &mut cfg)?;
        let mut b = win_cmd(c, &t.root, "cmake");
        b.args(["--build", t.build_dir, "-j", &c.cfg.jobs.to_string()]);
        c.run(&format!("{log}-build"), &mut b)?;
        // The build itself re-checks the hooks (game/CMakeLists.txt); its stamp says it did.
        let bd = t.root.join(t.build_dir).join("game");
        ensure!(bd.join("lift-hooks.ok").exists(), "{}: no lift-hooks.ok, the hook check did not run", bd.display());
        let exe = bd.join(if t.compat { "RaCACiT_compat.exe" } else { "RaCACiT.exe" });
        ensure!(exe.exists(), "no {}", exe.display());
        let _ = &t.recomp;
        put(&exe, &c.bin.join("win/builds").join(&t.dest))?;
        c.note(format!("- {}: built, hooks checked ({} bytes)", t.dest, fs::metadata(&exe)?.len()));
        // ~1.5 GB of objects per build: gone once the exe is out, unless --keep-builds.
        if !c.args.keep_builds { rm_rf(&t.root.join(t.build_dir))?; }
    }
    Ok(())
}

fn build_windows_launcher(c: &mut Ctx) -> Result<()> {
    c.step("Windows launcher");
    let tree = c.tree.clone();
    let w = c.cfg.win.as_ref().unwrap();
    let mut icon = sh(&c.cfg.python, &["-I", "launcher/make_icon.py", "launcher/build/icon.ico"]);
    fs::create_dir_all(tree.join("launcher/build"))?;
    icon.current_dir(&tree);
    c.run("launcher-icon", &mut icon)?;
    // launcher/build.sh's command, with this host's zig and the cross-built SDL2.
    let exe = c.bin.join("win/RaCACiT Launcher.exe");
    fs::create_dir_all(exe.parent().unwrap())?;
    let mut z = Command::new(&w.zig);
    z.current_dir(tree.join("launcher/src"))
        .args(["c++", "-target", "x86_64-windows-gnu", "-std=c++17", "-O2", "-municode", "-DUNICODE", "-D_UNICODE",
            "app.cpp", "gfx.cpp", "iso.cpp", "store.cpp", "util.cpp", "launcher.rc"])
        .arg(format!("-I{}", tree.join("tools/sdl2-win/include").display()))
        .arg(format!("-L{}", tree.join("tools/sdl2-win/lib").display()))
        .args(["-lSDL2", "-ld2d1", "-ldwrite", "-lwindowscodecs", "-lole32", "-loleaut32", "-luuid", "-lshell32",
            "-lshlwapi", "-lwinhttp", "-lbcrypt", "-lsetupapi", "-limm32", "-lversion", "-lwinmm", "-lgdi32", "-luser32",
            "-ladvapi32", "-Wl,--subsystem,windows", "-o"]).arg(&exe);
    // launcher/build.sh is the source of truth: stop if it changed its command.
    let bs = fs::read_to_string(tree.join("launcher/build.sh"))?;
    ensure!(bs.contains("app.cpp gfx.cpp iso.cpp store.cpp util.cpp launcher.rc") && bs.contains("-lsetupapi -limm32 -lversion -lwinmm"),
        "launcher/build.sh changed its compile command: update build_windows_launcher in racit-release");
    c.run("launcher-win", &mut z)?;
    c.note(format!("- RaCACiT Launcher.exe {}: built", c.ver));
    Ok(())
}

// ----------------------------------------------------------- reference ------

/// The last official Windows zip: Into the Nexus, shader caches and the build README come from it,
/// and the new zip's layout is compared with it.
fn reference_zip(c: &mut Ctx) -> Result<PathBuf> {
    let refdir = c.cfg.work.join("ref");
    fs::create_dir_all(&refdir)?;
    if let Some(f) = &c.args.carry_from {
        if f.ends_with(".zip") { return Ok(PathBuf::from(f)); }
    }
    let tag = match &c.args.carry_from {
        Some(t) => t.clone(),
        None => {
            // The newest published upstream release with a Windows launcher zip, older than this tag.
            let list = out(sh("gh", &["release", "list", "-R", &c.cfg.upstream, "-L", "30", "--exclude-drafts",
                "--json", "tagName", "-q", ".[].tagName"]))?;
            let mut found = None;
            for t in list.lines() {
                if t == c.args.tag { continue; }
                let a = out(sh("gh", &["release", "view", t, "-R", &c.cfg.upstream, "--json", "assets", "-q", ".assets[].name"]))?;
                if a.lines().any(|n| n.starts_with("RACiT_Windows_Launcher_") && n.ends_with(".zip")) { found = Some(t.to_string()); break; }
            }
            found.ok_or_else(|| anyhow!("no upstream release with a Windows zip; pass --carry-from"))?
        }
    };
    let d = refdir.join(&tag);
    fs::create_dir_all(&d)?;
    let have = fs::read_dir(&d)?.filter_map(|e| e.ok()).map(|e| e.path())
        .find(|p| p.file_name().map(|n| n.to_string_lossy().starts_with("RACiT_Windows_Launcher_")).unwrap_or(false));
    if let Some(p) = have { return Ok(p); }
    c.step(&format!("downloading the {tag} Windows zip (reference)"));
    out(sh("gh", &["release", "download", &tag, "-R", &c.cfg.upstream, "-p", "RACiT_Windows_Launcher_*.zip", "-D"]).arg(&d))?;
    fs::read_dir(&d)?.filter_map(|e| e.ok()).map(|e| e.path()).find(|p| p.extension().is_some_and(|e| e == "zip"))
        .ok_or_else(|| anyhow!("download of {tag}'s Windows zip failed"))
}

/// Normalized entry names: the top folder and build numbers made generic, so two releases compare.
fn shape(names: &[String]) -> BTreeSet<String> {
    names.iter().filter(|n| !n.ends_with('/')).map(|n| {
        let mut parts: Vec<String> = n.split('/').map(String::from).collect();
        if let Some(top) = parts.first_mut() { if top.starts_with("RACiT_") { *top = "RACiT_*".into(); } }
        for i in 1..parts.len() {
            if parts[i - 1] == "builds" {
                let p = &parts[i];
                if let Some(rest) = p.strip_prefix('v') {
                    let digits: String = rest.chars().take_while(|c| c.is_ascii_digit()).collect();
                    if !digits.is_empty() { parts[i] = format!("v*{}", &rest[digits.len()..]); }
                }
            }
        }
        parts.join("/")
    }).collect()
}

fn zip_names(z: &Path) -> Result<Vec<String>> {
    let a = zip::ZipArchive::new(fs::File::open(z)?)?;
    Ok(a.file_names().map(|n| n.map(|n| n.into_owned())).collect::<std::result::Result<Vec<String>, _>>()?)
}

// ------------------------------------------------------------- packaging ----

fn package_mac(c: &mut Ctx) -> Result<PathBuf> {
    c.step("macOS zip");
    let min = mac_min(c)?;
    let prefix = match c.cfg.mac.as_ref().and_then(|m| m.lib_prefix.clone()) {
        Some(p) => p,
        None => {
            let p = c.cfg.work.join(format!("macos-libs-{min}"));
            if !p.join("lib/libSDL3.dylib").exists() {
                c.step(&format!("macOS libraries for macOS {min} (once, kept in {})", p.display()));
                let mut b = sh("bash", &["mac/build_macos_libs.sh"]);
                b.arg(&p).current_dir(&c.tree).env("MACOS_MIN", &min);
                c.run("macos-libs", &mut b)?;
            }
            p
        }
    };
    let outdir = c.dist.join("mac");
    rm_rf(&outdir)?;
    let mut p = sh("bash", &["mac/package_macos.sh", "--version", &c.args.build.to_string(), "--game"]);
    p.arg(c.bin.join("mac-100/RaCACiTRecomp"));
    if c.cfg.inputs.us120_elf.is_some() { p.arg("--game120").arg(c.bin.join("mac-120/RaCACiTRecomp")); }
    p.arg("--out").arg(&outdir).current_dir(&c.tree).env("MACOS_MIN", &min).env("MACOS_LIB_PREFIX", &prefix);
    c.note(format!("- macOS zip targets macOS {min} and later"));
    c.run("package-macos", &mut p)?;
    let name = format!("RACiT-{}-macos.zip", c.ver);
    let z = c.dist.join(&name);
    fs::rename(outdir.join(&name), &z)?;
    rm_rf(&outdir)?;
    c.note(format!("- {name}: {} bytes, sha256 {}", fs::metadata(&z)?.len(), sha256(&z)?));
    Ok(z)
}

fn package_windows(c: &mut Ctx, reference: &Path) -> Result<PathBuf> {
    c.step("Windows zip");
    let top = format!("RACiT_{}", c.ver);
    let stage = c.dist.join("win-stage");
    rm_rf(&stage)?;
    let root = stage.join(&top);
    let n = c.args.build;
    let tree = c.tree.clone();

    // From the tag: the launcher, its README, the built-in mods.
    put(&c.bin.join("win/RaCACiT Launcher.exe"), &root.join("RaCACiT Launcher.exe"))?;
    put(&tree.join("launcher/README.txt"), &root.join("README.txt"))?;
    let mut mods = vec![];
    walk(&tree.join("launcher/mods"), &tree.join("launcher/mods"), &mut mods)?;
    for m in mods {
        let from = tree.join("launcher/mods").join(&m);
        if from.is_file() { put(&from, &root.join("mods").join(&m))?; }
    }
    // The games just built.
    let mut built = vec![];
    walk(&c.bin.join("win/builds"), &c.bin.join("win/builds"), &mut built)?;
    for b in &built {
        let from = c.bin.join("win/builds").join(b);
        if from.is_file() { put(&from, &root.join("builds").join(b))?; }
    }
    for d in [format!("v{n}"), format!("v{n}-u120")] {
        if root.join("builds").join(&d).is_dir() { fs::write(root.join("builds").join(&d).join("version.txt"), format!("{n}\r\n"))?; }
    }

    // From the last official zip: Into the Nexus (built elsewhere), the shader caches the launcher
    // seeds from, and the build README's history.
    let mut a = zip::ZipArchive::new(fs::File::open(reference)?)?;
    let mut prev_readme = String::new();
    let mut carried = vec![];
    for i in 0..a.len() {
        let mut f = a.by_index(i)?;
        if f.is_dir() { continue; }
        let name = f.name()?.into_owned();
        let rel: Vec<&str> = name.splitn(2, '/').collect();
        if rel.len() != 2 { continue; }
        let rel = rel[1];
        let dest = if rel.starts_with("nexus/") {
            Some(root.join(rel))
        } else if let Some(r) = rel.strip_prefix("builds/") {
            let (dir, file) = r.split_once('/').unwrap_or(("", ""));
            if dir.starts_with('v') && !dir.contains('-') {
                match file {
                    "shadercache.bin" | "pipelinelist.bin" => Some(root.join(format!("builds/v{n}/{file}"))),
                    "README.txt" => { f.read_to_string(&mut prev_readme)?; None }
                    _ => None,
                }
            } else { None }
        } else { None };
        if let Some(d) = dest {
            fs::create_dir_all(d.parent().unwrap())?;
            let mut w = fs::File::create(&d)?;
            std::io::copy(&mut f, &mut w)?;
            carried.push(rel.to_string());
        }
    }
    c.note(format!("- carried from {}: {} files (nexus/, shader caches)", reference.file_name().unwrap().to_string_lossy(), carried.len()));

    // builds\vN\README.txt: the new section on top of the history, as the official zips have it.
    let head = match &c.args.build_notes {
        Some(p) => fs::read_to_string(p)?,
        None => {
            c.note("- builds README: no --build-notes, a generic header was written (write real notes before publishing)");
            let t = format!("Ratchet & Clank: A Crack in Time -- Windows build v{n} (RACiT {})", c.ver);
            format!("{t}\n{}\n\nSee the release notes of RACiT {} for what changed.\n", "=".repeat(t.len()), c.ver)
        }
    };
    let readme = format!("{}\n\n{}", head.trim_end(), prev_readme.trim_start_matches('\u{feff}'));
    fs::write(root.join(format!("builds/v{n}/README.txt")), readme.replace("\r\n", "\n").replace('\n', "\r\n"))?;

    // Zip it.
    let name = format!("RACiT_Windows_Launcher_{}.zip", c.ver);
    let z = c.dist.join(&name);
    let mut files = vec![];
    walk(&stage, &stage, &mut files)?;
    let mut w = zip::ZipWriter::new(fs::File::create(&z)?);
    let opts = zip::write::SimpleFileOptions::default()
        .compression_method(zip::CompressionMethod::Deflated).large_file(false);
    let mut buf = vec![];
    for f in &files {
        let full = stage.join(f);
        let entry = f.to_string_lossy().replace('\\', "/");
        if full.is_dir() {
            w.add_directory(format!("{entry}/"), opts)?;
        } else {
            let exe = entry.ends_with(".exe");
            w.start_file(&entry, opts.unix_permissions(if exe { 0o755 } else { 0o644 }))?;
            buf.clear();
            fs::File::open(&full)?.read_to_end(&mut buf)?;
            w.write_all(&buf)?;
        }
    }
    w.finish()?;
    rm_rf(&stage)?;

    // Layout against the official zip.
    let ours = shape(&zip_names(&z)?);
    let theirs = shape(&zip_names(reference)?);
    let missing: Vec<_> = theirs.difference(&ours).cloned().collect();
    let extra: Vec<_> = ours.difference(&theirs).cloned().collect();
    c.note(format!("- {name}: {} bytes, sha256 {}", fs::metadata(&z)?.len(), sha256(&z)?));
    c.note(format!("- layout vs {}: {} files in both; new: {}; missing: {}",
        reference.file_name().unwrap().to_string_lossy(), ours.intersection(&theirs).count(),
        if extra.is_empty() { "none".into() } else { extra.join(", ") },
        if missing.is_empty() { "none".into() } else { missing.join(", ") }));
    let unexpected: Vec<_> = missing.iter().filter(|m| !(c.args.allow_missing_eu && m.contains("BCES00511"))).collect();
    ensure!(unexpected.is_empty(), "the Windows zip lacks files the last official one had: {unexpected:?}");
    let odd: Vec<_> = extra.iter().filter(|e| !e.contains("/builds/v*-u120/") && !e.starts_with("RACiT_*/mods/")).collect();
    ensure!(odd.is_empty(), "the Windows zip has files the official layout doesn't: {odd:?}");
    Ok(z)
}

// ----------------------------------------------------------------- smoke ----

fn smoke_mac(c: &mut Ctx, z: &Path) -> Result<()> {
    c.step("smoke: macOS zip from a fresh unzip");
    let d = c.out.join("smoke/mac");
    rm_rf(&d)?;
    fs::create_dir_all(&d)?;
    out(sh("ditto", &["-x", "-k"]).arg(z).arg(&d))?;
    let rel = d.join(format!("RACiT-{}-macos", c.ver));
    let launcher = rel.join("RaCACiT Launcher.app/Contents/MacOS/RaCACiT Launcher");
    let mut st = Command::new(&launcher);
    st.arg("--self-test").env("SDL_AUDIODRIVER", "dummy");
    c.run("smoke-mac-launcher", &mut st)?;
    c.note("- macOS launcher --self-test from the zip: PASS");

    let s = &c.cfg.smoke;
    let runs: Vec<(&str, PathBuf, Vec<String>)> = {
        let mut v = vec![];
        if let (Some(g), Some(inst)) = (&s.game, &s.installed_100) {
            v.push(("1.00", rel.join(format!("builds/v{}", c.args.build)),
                vec!["--elf".into(), c.cfg.inputs.us100_elf.display().to_string(), "--game".into(), g.display().to_string(),
                     "--installed".into(), inst.display().to_string()]));
        }
        if let (Some(g), Some(inst), Some(u), Some(e)) = (&s.game, &s.installed_120, &s.update, &c.cfg.inputs.us120_elf) {
            v.push(("1.20", rel.join(format!("builds/v{}-u120", c.args.build)),
                vec!["--elf".into(), e.display().to_string(), "--update".into(), u.display().to_string(), "--game".into(),
                     g.display().to_string(), "--installed".into(), inst.display().to_string()]));
        }
        v
    };
    if runs.is_empty() { c.note("- macOS game launch: NOT RUN (no [smoke] game/installed paths)"); }
    for (ver, build, extra) in runs {
        if let Some(why) = owner_busy() { c.note(format!("- macOS {ver} launch: SKIPPED ({why})")); c.shippable = false; continue; }
        let mut verdict = String::from("FAIL");
        for cap in ["600", "1200"] {
            let work = c.out.join(format!("smoke/mac-run-{ver}"));
            rm_rf(&work)?;
            let mut t = sh(&c.cfg.python, &["-I", "tests/mac_user_launch.py", "--build"]);
            t.arg(&build).args(&extra).arg("--work").arg(&work).args(["--seconds", "60", "--cap", cap])
                .current_dir(&c.tree).env("SDL_AUDIODRIVER", "dummy");
            let log = c.logs.join(format!("smoke-mac-{ver}-{cap}.log"));
            let f = fs::File::create(&log)?;
            let code = t.stdin(Stdio::null()).stdout(f.try_clone()?).stderr(f).status()?.code();
            rm_rf(&work)?;
            match code {
                Some(0) => { verdict = "PASS".into(); break; }
                Some(2) => { verdict = format!("INCONCLUSIVE at cap {cap} s"); }
                _ => { verdict = format!("FAIL (exit {code:?}, {})", log.display()); break; }
            }
        }
        c.note(format!("- macOS {ver} user-style launch (tests/mac_user_launch.py, offscreen, muted): {verdict}"));
        if verdict != "PASS" { c.shippable = false; }
    }
    rm_rf(&d)?;
    Ok(())
}

fn smoke_windows(c: &mut Ctx, z: &Path) -> Result<()> {
    c.step("smoke: Windows zip from a fresh unzip");
    let d = c.out.join("smoke/win");
    rm_rf(&d)?;
    fs::create_dir_all(&d)?;
    zip::ZipArchive::new(fs::File::open(z)?)?.extract(&d)?;
    let rel = d.join(format!("RACiT_{}", c.ver));
    let (wine, wineprefix) = (c.cfg.smoke.wine.clone(), c.cfg.smoke.wineprefix.clone());
    let (racit_wine, racit_wine_scratch) = (c.cfg.smoke.racit_wine.clone(), c.cfg.smoke.racit_wine_scratch.clone());
    match &wine {
        Some(wine) => {
            let mut t = Command::new(wine);
            t.arg(rel.join("RaCACiT Launcher.exe")).arg("--self-test").env("SDL_AUDIODRIVER", "dummy")
                .env("WINEDEBUG", "-all");
            if let Some(p) = &wineprefix { t.env("WINEPREFIX", p); }
            c.run("smoke-win-launcher", &mut t)?;
            c.note("- Windows launcher --self-test from the zip (Wine): PASS");
        }
        None => c.note("- Windows launcher --self-test: NOT RUN (no [smoke] wine)"),
    }
    match (&racit_wine, &racit_wine_scratch) {
        (Some(rw), Some(scr)) => {
            if let Some(why) = owner_busy() {
                c.note(format!("- Windows 1.00 game run (Wine): SKIPPED ({why})"));
                c.shippable = false;
            } else {
                // Rosetta has no AVX2: only the compat program runs under Wine on a Mac.
                let exe = rel.join(format!("builds/v{}/RaCACiT_compat.exe", c.args.build));
                let name = format!("relsmoke-{}", c.ver);
                let mut t = Command::new(rw);
                t.arg(&exe).arg(scr).arg(&name).args(["--watch", "120"]).env("SDL_AUDIODRIVER", "dummy");
                let log = c.logs.join("smoke-win-game.log");
                let f = fs::File::create(&log)?;
                let st = t.stdin(Stdio::null()).stdout(f.try_clone()?).stderr(f).status()?;
                let summary = fs::read_to_string(&log).unwrap_or_default();
                let _ = rm_rf(&scr.join(&name));
                let fails: Vec<&str> = summary.lines().filter(|l| l.contains(" FAIL") || l.contains("INVALID")).collect();
                let passes = summary.lines().filter(|l| l.contains(" PASS ")).count();
                let ok = st.success() && fails.is_empty() && passes >= 2;
                c.note(format!("- Windows 1.00 RaCACiT_compat.exe under Wine (racit-wine --watch 120, offscreen, muted): {} ({passes} checks passed{})",
                    if ok { "PASS" } else { "FAIL" }, if fails.is_empty() { String::new() } else { format!("; {}", fails.join(" | ")) }));
                if !ok { c.shippable = false; }
            }
        }
        _ => c.note("- Windows game run: NOT RUN (no [smoke] racit_wine)"),
    }
    c.note("- Windows AVX2 programs, 1.20 and EU programs: not run here (Rosetta has no AVX2; racit-wine runs 1.00 only). Test on Windows before publishing.");
    rm_rf(&d)?;
    Ok(())
}

// ---------------------------------------------------------------- upload ----

fn upload(c: &mut Ctx, commit: &str, assets: &[PathBuf]) -> Result<()> {
    c.step("draft release on the fork");
    let fork = c.cfg.fork.clone();
    ensure!(fork != c.cfg.upstream, "the fork and upstream are the same repository: refusing");
    out(sh("gh", &["api", &format!("repos/{fork}/commits/{commit}"), "-q", ".sha"]))
        .with_context(|| format!("{fork} doesn't have {commit} yet: sync the fork's main with upstream first"))?;
    let existing = Command::new("gh").args(["release", "view", &c.args.tag, "-R", &fork, "--json", "isDraft", "-q", ".isDraft"])
        .output()?;
    let notes = c.out.join("release-notes.md");
    let mut body = String::new();
    if let Some(p) = &c.args.release_notes { body.push_str(&fs::read_to_string(p)?); body.push_str("\n\n"); }
    let _ = writeln!(body, "<!-- racit-release build report -->\n### Build report (racit-release)\n");
    body.push_str(&c.report);
    if !c.shippable { body.push_str("\n**Not shippable as is: see the report above.**\n"); }
    fs::write(&notes, &body)?;
    if existing.status.success() {
        let draft = String::from_utf8_lossy(&existing.stdout).trim() == "true";
        ensure!(draft, "{fork} already has a PUBLISHED release {}: not touching it", c.args.tag);
        let mut up = sh("gh", &["release", "upload", &c.args.tag, "-R", &fork, "--clobber"]);
        up.args(assets);
        c.run("gh-upload", &mut up)?;
        let mut ed = sh("gh", &["release", "edit", &c.args.tag, "-R", &fork, "--draft", "--notes-file"]);
        ed.arg(&notes);
        c.run("gh-edit", &mut ed)?;
    } else {
        // A draft with --target: GitHub makes the tag only when someone publishes, so nothing is
        // pushed and no tag-triggered workflow runs.
        let mut cr = sh("gh", &["release", "create", &c.args.tag, "-R", &fork, "--draft", "--target", commit,
            "--title", &format!("RACiT v{}", c.ver), "--notes-file"]);
        cr.arg(&notes).args(assets);
        c.run("gh-create", &mut cr)?;
    }
    let url = out(sh("gh", &["release", "view", &c.args.tag, "-R", &fork, "--json", "url", "-q", ".url"]))?;
    c.note(format!("- draft release: {url}"));
    Ok(())
}

// ------------------------------------------------------------------ main ----

fn run() -> Result<()> {
    let args = parse_args()?;
    if !args.no_queue_check {
        ensure!(std::env::var_os("AGENTQ_JOB_ID").is_some(),
            "run this inside the job queue (agentq run --label rccit-rel-build --mem 10 -- racit-release ...), or pass --no-queue-check");
    }
    let text = fs::read_to_string(&args.config).with_context(|| format!("config {}", args.config.display()))?;
    let cfg: Config = toml::from_str(&text).with_context(|| format!("config {}", args.config.display()))?;
    ensure!(!args.mac || cfg!(target_os = "macos"), "the macOS zip needs a Mac (use --only win elsewhere)");
    let tree = cfg.tree.clone().unwrap_or_else(|| cfg.repo.join(".worktrees/release-build"));
    let ver = args.tag[1..].to_string();
    // A --ref preflight gets its own folder, so it never replaces a release's zips.
    let out_dir = match &args.git_ref {
        Some(r) => cfg.work.join(format!("{}-ref-{}", args.tag, r.replace('/', "-"))),
        None => cfg.work.join(&args.tag),
    };
    let mut c = Ctx {
        logs: out_dir.join("logs"), bin: out_dir.join("bin"), dist: out_dir.join("dist"), out: out_dir,
        cfg, args, ver, tree, report: String::new(), shippable: true, started: Instant::now(),
    };
    rm_rf(&c.bin)?;
    for d in [&c.logs, &c.bin, &c.dist] { fs::create_dir_all(d)?; }
    c.note(format!("RACiT {} build v{} ({})", c.ver, c.args.build,
        out(&mut sh("date", &["+%Y-%m-%d %H:%M %z"])).unwrap_or_default()));
    c.need_space("start")?;
    if c.args.win { write_machost_toolchain(&c)?; }
    let reference = if c.args.win { Some(reference_zip(&mut c)?) } else { None };
    let commit = prepare_tree(&mut c)?;
    pipelines(&mut c)?;
    let mut assets = vec![];
    if c.args.mac { assets.push(package_mac(&mut c)?); }
    if c.args.win {
        build_windows(&mut c)?;
        build_windows_launcher(&mut c)?;
        assets.push(package_windows(&mut c, reference.as_deref().unwrap())?);
    }
    if !c.args.keep_builds {
        // The Windows objects are ~1.5 GB per build; the lifts and Mac builds stay for the next run.
        for d in ["game/build-win", "game/build-win-compat", "game/build-win-u120", "game/build-win-u120-compat", "build/BCES00511"] {
            rm_rf(&c.tree.join(d))?;
        }
    }
    if c.args.smoke {
        for z in assets.clone() {
            if z.to_string_lossy().ends_with("-macos.zip") { smoke_mac(&mut c, &z)?; } else { smoke_windows(&mut c, &z)?; }
        }
    } else {
        c.shippable = false;
        c.note("- smoke tests: NOT RUN (--no-smoke)");
    }
    let mins = c.started.elapsed().as_secs() / 60;
    c.note(format!("- total {mins} min; {}", if c.shippable { "all checks passed" } else { "NOT shippable as is (see above)" }));
    fs::write(c.out.join("summary.md"), &c.report)?;
    if c.args.upload { upload(&mut c, &commit, &assets)?; fs::write(c.out.join("summary.md"), &c.report)?; }
    // The built programs are inside the zips now.
    if !c.args.keep_builds { rm_rf(&c.bin)?; }
    if let Some(r) = std::env::var_os("AGENTQ_RESULT") { let _ = fs::write(r, &c.report); }
    println!("\nsummary: {}", c.out.join("summary.md").display());
    Ok(())
}

fn main() {
    if let Err(e) = run() {
        eprintln!("racit-release: {e:#}");
        std::process::exit(1);
    }
}

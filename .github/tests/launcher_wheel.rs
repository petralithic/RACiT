use std::env;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::thread;
use std::time::{Duration, Instant};

struct Scratch(PathBuf);

impl Drop for Scratch {
    fn drop(&mut self) {
        let _ = fs::remove_dir_all(&self.0);
    }
}

fn seed_game(root: &Path) {
    let disc = root.join("game/PS3_GAME");
    fs::create_dir_all(disc.join("USRDIR")).unwrap();
    let mut elf = [0; 20];
    elf[..6].copy_from_slice(b"\x7fELF\x02\x02");
    elf[19] = 0x15;
    fs::write(disc.join("USRDIR/EBOOT.elf"), elf).unwrap();

    // Minimal launcher metadata. The stub never reads or executes the ELF.
    let title = b"BCUS98124\0";
    let mut sfo = b"\0PSF".to_vec();
    for value in [
        0x0101u32,
        36,
        45,
        1,
        0x0204_0000,
        title.len() as u32,
        title.len() as u32,
        0,
    ] {
        sfo.extend(value.to_le_bytes());
    }
    sfo.extend(b"TITLE_ID\0");
    sfo.extend(title);
    fs::write(disc.join("PARAM.SFO"), sfo).unwrap();
}

fn replay(launcher: &Path, root: &Path, mode: &str, sensitivity: &str) {
    let script = root.join("launcher-script.txt");
    let probe = root.join("data/wheel-launch.txt");
    if probe.exists() {
        fs::remove_file(&probe).unwrap();
    }

    let mut keys = String::from("key DOWN\nkey ENTER\n");
    // Reach the wheel setting after the existing key bindings row.
    keys.push_str(&"key DOWN\n".repeat(20));
    keys.push_str("key RIGHT\nkey DOWN\nkey LEFT\nkey ESC\nkey ENTER\nwait 1500\nquit\n");
    fs::write(&script, keys).unwrap();
    let mut command = Command::new(launcher);
    command.env("RACACIT_LAUNCHER_SCRIPT", &script);
    if cfg!(target_os = "macos") {
        command.arg("--root").arg(root);
    }
    let mut child = command.spawn().unwrap();
    let deadline = Instant::now() + Duration::from_secs(25);
    loop {
        if let Some(status) = child.try_wait().unwrap() {
            assert!(status.success(), "launcher exited {status}");
            break;
        }
        if Instant::now() >= deadline {
            child.kill().unwrap();
            child.wait().unwrap();
            panic!("launcher did not finish the settings replay");
        }
        thread::sleep(Duration::from_millis(100));
    }

    let disk = fs::read_to_string(root.join("data/settings.txt")).unwrap();
    let expected = [
        format!("RACACIT_WEAPON_SCROLL={mode}"),
        format!("RACACIT_SCROLL_SENS={sensitivity}"),
    ];
    for setting in &expected {
        assert!(
            disk.lines().any(|line| line == setting),
            "launcher did not save {setting}; settings were {disk:?}",
        );
    }
    assert!(disk.lines().any(|line| line == "RACACIT_LANGUAGE=fr"));
    let passed = fs::read_to_string(probe).expect("Play did not start the stub");
    assert_eq!(passed, format!("{}\n", expected.join("\n")));
    println!("Settings saved and passed to Play: mode={mode}, sensitivity={sensitivity}");
}

fn main() {
    if env::var("RACACIT_LAUNCHER").as_deref() == Ok("1") {
        let settings = PathBuf::from(env::var_os("RACACIT_SETTINGS_FILE").unwrap());
        let mode = env::var("RACACIT_WEAPON_SCROLL").unwrap();
        let sensitivity = env::var("RACACIT_SCROLL_SENS").unwrap();
        fs::write(
            settings.with_file_name("wheel-launch.txt"),
            format!("RACACIT_WEAPON_SCROLL={mode}\nRACACIT_SCROLL_SENS={sensitivity}\n"),
        )
        .unwrap();
        return;
    }

    let launcher =
        fs::canonicalize(env::args_os().nth(1).expect("native launcher executable")).unwrap();
    let root = env::temp_dir().join(format!("racit-wheel-settings-{}", std::process::id()));
    fs::create_dir(&root).unwrap();
    let scratch = Scratch(root);
    let root = &scratch.0;
    let game = root.join("builds/v125");
    fs::create_dir_all(&game).unwrap();
    fs::create_dir_all(root.join("data")).unwrap();
    fs::write(
        root.join("launcher.ini"),
        "check_updates=0\nwhile_playing=stay\nbackup_on_play=0\n",
    )
    .unwrap();
    fs::write(root.join("data/settings.txt"), "RACACIT_LANGUAGE=fr\n").unwrap();
    seed_game(root);

    let probe = env::current_exe().unwrap();
    let launcher = if cfg!(windows) {
        fs::copy(launcher.with_file_name("SDL2.dll"), root.join("SDL2.dll")).unwrap();
        fs::copy(&probe, game.join("RaCACiT.exe")).unwrap();
        fs::copy(&probe, game.join("RaCACiT_compat.exe")).unwrap();
        let copied = root.join(launcher.file_name().unwrap());
        fs::copy(launcher, &copied).unwrap();
        copied
    } else {
        fs::copy(probe, game.join("RaCACiT")).unwrap();
        launcher
    };
    replay(&launcher, root, "2", "75");
    replay(&launcher, root, "0", "50");
}

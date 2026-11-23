# Releasing RACiT

`release/racit-release` is one command that builds every release asset for a tag, checks each
zip from a fresh unzip, and attaches them to a **draft** GitHub release on the fork. Publishing,
and anything upstream, stays a manual step.

Hosted CI can't do this: the game is lifted from the player's own decrypted EBOOT, and that
can't go on GitHub. So the builder runs on a maintainer's Mac, on demand, inside the machine's
job queue. Nothing stays running between releases.

## What it makes

| Asset | Contents |
| --- | --- |
| `RACiT-<ver>-macos.zip` | `mac/package_macos.sh`: the launcher app, `builds/vN` (1.00) and `builds/vN-u120` (update 1.20) with their libraries |
| `RACiT_Windows_Launcher_<ver>.zip` | `RACiT_<ver>/`: `RaCACiT Launcher.exe`, `README.txt`, `mods/`, `builds/vN/` (`RaCACiT.exe`, `RaCACiT_compat.exe`, `RaCACiT_BCES00511.exe`, `RaCACiT_BCES00511_compat.exe`, `shadercache.bin`, `pipelinelist.bin`, `version.txt`, `README.txt`), `builds/vN-u120/` (`RaCACiT.exe`, `RaCACiT_compat.exe`, `version.txt`) and `nexus/` |

The Windows layout follows the last official Windows zip (1.0.1 when this was written), and
the builder compares each new zip with it. A file the old zip had and the new one lacks stops
the build. The only additions allowed are `builds/vN-u120/` and new mods. Three things come
from that reference zip and are not built here:

- `nexus/`: Into the Nexus, built elsewhere;
- `shadercache.bin` and `pipelinelist.bin`: the caches the launcher seeds `data\cache` from;
- the build README's history. `--build-notes FILE` is the new section on top. Without it, the
  builder writes a generic header, which is fine for a test draft but should be replaced
  before publishing.

## One-time setup (macOS, Apple Silicon)

- Xcode command line tools, Homebrew `cmake`, `ninja`, `zig@0.16` (0.17 rejects the game's
  patchable-function-entry flags), `llvm@22` (for `llvm-ar`/`llvm-ranlib`/`llvm-rc`: the repo's
  `.cmd` wrappers only run on Windows), `gh` logged in with write access to the fork.
- The macOS library prefix built for macOS 14 (`mac/build_macos_libs.sh`).
- For the smoke tests: a decrypted disc folder and HDD installs (see `tests/mac_user_launch.py`),
  Wine with a null-display prefix, and `racit-wine` (rccit-recomp `rust/racit-wine.rs`).
- Copy `release/racit-release/config.example.toml` to `~/.config/racit-release/config.toml` and
  fill it in. It names your game dumps, so it stays out of the repository. The builder checks
  each EBOOT against `versions/profiles.json` before building.
- Build the tool: `cargo build --release --manifest-path release/racit-release/Cargo.toml`.

## Making a release

1. The tag exists upstream, with `kLauncherVersion` in `launcher/src/app.cpp` matching it (the
   builder stops otherwise). The fork must have the tagged commit (sync its `main`).
2. Pick the build number: the next after the one shipped (1.0.0 shipped Mac v117 and Windows
   v124; 1.0.2 is v125 on both).
3. Run it in the queue:

   ```bash
   agentq run --label rccit-rel-build --project ~/Projects/rccit-recomp --mem 10 --timeout 18000 -- \
     racit-release --tag v1.0.3 --build 126 --build-notes notes-v126.txt --release-notes notes.md
   ```

   Options: `--only mac|win`; `--carry-from TAG|ZIP` (the reference Windows zip; default: the
   newest upstream release that has one); `--allow-missing-eu` (test builds without the
   European EBOOT: the report marks the release not shippable); `--no-smoke`; `--no-upload`;
   `--keep-builds` (keep the Windows object folders, about 1.5 GB each).
4. Read `<work>/<tag>/summary.md` (it also goes into the draft's notes), test the Windows
   programs on real Windows (see below), then publish or hand the zips over by hand.

## What the builder does

1. **Tree.** A worktree of `repo` owned by the builder (`.worktrees/release-build`), checked
   out detached at the tag. It stops if someone edited tracked files there. It runs
   `mac/setup_macos.sh` (runtime at its pin, the tag's runtime patch), fetches the Windows
   sources at `scripts/setup_windows.sh`'s pins, cross-builds static SDL2, and copies the
   inputs into `input/`.
2. **Lifts and Mac games.** `scripts/pipeline.sh` for 1.00, then for the update 1.20 EBOOT.
3. **Windows games**, cross-built with zig 0.16. 1.00 and 1.20 each get an AVX2 and an older-CPU
   build (`-DRACACIT_CPU_COMPAT=ON`), each in its own build folder. With the European EBOOT, the
   same goes for `scripts/retarget.py BCES00511`'s converted copy (the steps of
   `scripts/build_version.sh`). Every lift passes `scripts/check_lift_hooks.py` before its exes
   are taken, and each build must leave its `lift-hooks.ok` stamp. Then the launcher exe, with
   `launcher/build.sh`'s command.
4. **Zips.** `mac/package_macos.sh` for the Mac. For Windows, the layout above, zipped and
   compared with the reference zip.
5. **Smoke tests**, from a fresh unzip of each zip, muted and offscreen:
   - Mac: the launcher's `--self-test`, then `tests/mac_user_launch.py` on `builds/vN` and
     `builds/vN-u120` (an INCONCLUSIVE run is retried once with a longer cap);
   - Windows: the launcher's `--self-test` under Wine, and `racit-wine --watch 120` on
     `builds/vN/RaCACiT_compat.exe`.
   Game runs are skipped (and the release marked not shippable) while the owner plays or is on a
   call.
6. **Draft release** on the fork: created with `--target <commit>`, so no tag is pushed and no
   tag workflow runs. A rerun replaces the assets of an existing draft. A published release is
   never touched.

## What it doesn't check

- The AVX2 Windows programs, and the 1.20 and EU Windows programs, aren't run: Rosetta has no
  AVX2, and `racit-wine` runs the 1.00 disc only. Run them on a Windows PC before publishing.
- The Windows launcher's window (Direct2D) doesn't open under Wine on macOS; only `--self-test` runs.
- The builder only builds the Windows zip from a Mac; on a Windows host, `scripts/setup_windows.sh`
  and `scripts/build_version.sh` remain the manual route.

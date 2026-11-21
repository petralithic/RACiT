import os
from pathlib import Path
import shutil
import subprocess
import tempfile

from test_lift_hooks import CheckLiftHooks, ROOT, hook_list


def run(*args, fail=False):
    done = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding="utf-8")
    if (done.returncode != 0) != fail:
        raise RuntimeError(done.stdout)
    return done.stdout


def hook_block(text):
    start = text.index("\nif(RACACIT_CHECK_HOOKS)\n")
    end = text.index("# Android: the dual-screen", start)
    return text[start:end]


def main():
    old = hook_block(run("git", "show", "1aef2e4:game/CMakeLists.txt"))
    new = hook_block((ROOT / "game" / "CMakeLists.txt").read_text(encoding="utf-8"))
    generators = ["Ninja"] if os.name == "nt" else ["Ninja", "Unix Makefiles"]
    with tempfile.TemporaryDirectory(prefix="lift content ") as temporary:
        work = Path(temporary)
        for name in ("scripts", "versions"):
            shutil.copytree(ROOT / name, work / name)
        game = work / "game"
        game.mkdir()
        shutil.copyfile(ROOT / "game" / "CheckLiftHooks.cmake", game / "CheckLiftHooks.cmake")
        for generator in generators:
            for profile in ("BCUS98124-1.00", "BCUS98124-1.20"):
                hooks, functions = hook_list(profile)
                scanned = f"check_lift_hooks: {profile}: all {len(hooks)} expected hooks present"
                for label, block, expected_repeat in (("old", old, 1), ("new", new, 0)):
                    build = work / f"{generator}-{profile}-{label}"
                    lift = build / "synthetic lift"
                    lift.mkdir(parents=True)
                    fixture = CheckLiftHooks()
                    fixture.lift(lift, hooks, functions, chunks=2)
                    (build / "lift-patched.ok").touch()
                    (game / "CMakeLists.txt").write_text(
                        "cmake_minimum_required(VERSION 3.20)\n"
                        "project(RaCACiTRecomp NONE)\n"
                        "add_custom_target(RaCACiTRecomp ALL)\n"
                        "set(RACACIT_CHECK_HOOKS ON)\n"
                        "set(_racit_profile \"${LIFT_PROFILE}\")\n"
                        "file(GLOB RECOMP_SOURCES CONFIGURE_DEPENDS \"${RECOMP_DIR}/ppu_recomp_*.cpp\")\n"
                        + block,
                        encoding="utf-8",
                    )
                    configure = (
                        "cmake", "-S", str(game), "-B", str(build), "-G", generator,
                        f"-DRECOMP_DIR={lift.as_posix()}", f"-DLIFT_PROFILE={profile}",
                    )
                    first = run(*configure)
                    assert first.count(scanned) == 1, first
                    again = run(*configure)
                    assert again.count(scanned) == expected_repeat, again
                    if label == "new":
                        assert "unchanged since verification" in again, again
                        built = run("cmake", "--build", str(build))
                        assert scanned not in built, built

                        # Change an actual required call while preserving source timestamps.
                        source_paths = sorted(lift.glob("ppu_recomp_*.cpp"))
                        original_times = {path: path.stat() for path in source_paths}
                        removed = next(iter(hooks))
                        fixture.lift(lift, hooks, functions, leave_out=(removed,), chunks=2)
                        for path, timing in original_times.items():
                            os.utime(path, ns=(timing.st_atime_ns, timing.st_mtime_ns))
                        rejected = run(*configure, fail=True)
                        assert "lacks hooks" in rejected and removed in rejected, rejected
                        assert not (build / "lift-configured.ok.pending").exists()

                        fixture.lift(lift, hooks, functions, chunks=2)
                        for path, timing in original_times.items():
                            os.utime(path, ns=(timing.st_atime_ns, timing.st_mtime_ns))
                        restored = run(*configure)
                        assert "unchanged since verification" in restored, restored
                        print(f"{generator}, {profile}: rejected modified bytes with the original timestamps", flush=True)
                    print(f"{generator}, {profile}, {label}: {again.count(scanned)} repeat-configuration scans", flush=True)


if __name__ == "__main__":
    main()

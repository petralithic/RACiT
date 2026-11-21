import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

from test_lift_hooks import CheckLiftHooks, ROOT, hook_list


def run(args, *, expect_failure=False):
    result = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding="utf-8")
    if (result.returncode != 0) != expect_failure:
        raise RuntimeError(result.stdout)
    return result.stdout


def hook_block(text):
    start = text.index("\nif(RACACIT_CHECK_HOOKS)\n")
    end = text.index("# Android: the dual-screen", start)
    return text[start:end]


def main():
    baseline = hook_block(run(["git", "show", "f9c90e5:game/CMakeLists.txt"]))
    current = hook_block((ROOT / "game" / "CMakeLists.txt").read_text(encoding="utf-8"))
    generators = ["Ninja"] if os.name == "nt" else ["Ninja", "Unix Makefiles"]
    with tempfile.TemporaryDirectory(prefix="verify lift ") as temporary:
        work = Path(temporary)
        shutil.copytree(ROOT / "scripts", work / "scripts")
        shutil.copytree(ROOT / "versions", work / "versions")
        game = work / "game"
        game.mkdir()
        shutil.copyfile(ROOT / "game" / "CheckLiftHooks.cmake", game / "CheckLiftHooks.cmake")
        for generator in generators:
            for profile in ["BCUS98124-1.00", "BCUS98124-1.20"]:
                hooks, functions = hook_list(profile)
                output_line = f"check_lift_hooks: {profile}: all {len(hooks)} expected hooks present"
                for variant, block, expected_scans in [
                    ("baseline", baseline, 2), ("current", current, 1)
                ]:
                    build = work / f"{generator}-{profile}-{variant}"
                    build.mkdir()
                    lift = build / "synthetic lift"
                    lift.mkdir()
                    fixture = CheckLiftHooks()
                    fixture.lift(lift, hooks, functions, chunks=2)
                    # These files are already assembled with all required hooks.
                    # The stamp only skips applying the patcher, which needs a real executable.
                    (build / "lift-patched.ok").touch()
                    (game / "CMakeLists.txt").write_text(
                        "cmake_minimum_required(VERSION 3.20)\n"
                        "project(lift_check NONE)\n"
                        "add_custom_target(RaCACiTRecomp ALL)\n"
                        "set(RACACIT_CHECK_HOOKS ON)\n"
                        "set(_racit_profile \"${LIFT_PROFILE}\")\n"
                        "file(GLOB RECOMP_SOURCES CONFIGURE_DEPENDS \"${RECOMP_DIR}/ppu_recomp_*.cpp\")\n"
                        + block,
                        encoding="utf-8",
                    )
                    configured = run([
                        "cmake", "-S", str(game), "-B", str(build), "-G", generator,
                        f"-DRECOMP_DIR={lift}", f"-DLIFT_PROFILE={profile}",
                    ])
                    built = run(["cmake", "--build", str(build)])
                    assert configured.count(output_line) == 1, configured
                    scans = (configured + built).count(output_line)
                    assert scans == expected_scans, built

                    if variant == "current":
                        time.sleep(1.1)
                        removed = next(iter(hooks))
                        fixture.lift(lift, hooks, functions, leave_out=(removed,), chunks=2)
                        rejected = run(["cmake", "--build", str(build)], expect_failure=True)
                        assert "lacks hooks" in rejected and removed in rejected, rejected

                        fixture.lift(lift, hooks, functions, chunks=2)
                        rechecked = run(["cmake", "--build", str(build)])
                        assert rechecked.count(output_line) == 1, rechecked
                    print(f"{generator}, {profile}, {variant}: {scans} full hook scans before compilation", flush=True)


if __name__ == "__main__":
    main()

from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile


root = Path(__file__).resolve().parents[1]
game = root / "game"
toolchain = sys.argv[1]
parent = (root / "windows/CMakeLists.txt").read_text(encoding="utf-8")
target = (game / "CMakeLists.txt").read_text(encoding="utf-8")
manifest_rule = target.split("# The launcher hands off Unicode paths.", 1)[1]
manifest_rule = manifest_rule[manifest_rule.index("if(WIN32)") :]
manifest_rule = manifest_rule.split("target_include_directories", 1)[0]
parent_flags = next(line for line in parent.splitlines() if "add_compile_options" in line)
game_flags = target.split("# Compiler-specific flags", 1)[1].split(
    "# The Vulkan RSX backend", 1
)[0]

with tempfile.TemporaryDirectory(prefix="racit-unicode-") as temp:
    scratch = Path(temp)
    for name in ("racacit.rc", "racacit.manifest"):
        shutil.copyfile(game / name, scratch / name)
    (scratch / "CMakeLists.txt").write_text(
        "cmake_minimum_required(VERSION 3.22)\n"
        "project(path_probe C CXX)\n"
        "if(NOT MSVC)\n" + parent_flags + "\nendif()\n"
        "add_executable(path_probe probe.cpp)\n"
        'target_include_directories(path_probe PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")\n'
        + manifest_rule
        + "\n# " + game_flags,
        encoding="utf-8",
    )
    (scratch / "probe.cpp").write_text(
        r"""
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

int require(bool ok, int code) {
    if (!ok) std::fprintf(stderr, "Path check %d failed (Win32 %lu)\n", code, GetLastError());
    return ok ? 0 : code;
}

int main(int argc, char **argv) {
    std::printf("ACP=%u\n", GetACP());
    if (int error = require(GetACP() == CP_UTF8, 1)) return error;
    const char *path = std::getenv("PS3_VFS_ROOT");
    if (int error = require(argc == 2 && path && !std::strcmp(path, argv[1]), 2)) return error;
    if (int error = require(GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES, 3)) return error;
    std::FILE *disc = std::fopen(path, "rb");
    if (int error = require(disc != nullptr, 4)) return error;
    const int marker = std::fgetc(disc);
    std::fclose(disc);
    if (int error = require(marker == 'R', 5)) return error;
    char process[4096];
    const DWORD length = GetModuleFileNameA(nullptr, process, sizeof(process));
    if (int error = require(length > 0 && length < sizeof(process) &&
        std::strstr(process, "\xe6\x97\xa5\xe6\x9c\xac") != nullptr, 6)) return error;
    const char *save = std::getenv("PS3_HDD0_HOME_ROOT");
    std::FILE *dest = save ? std::fopen(save, "wb") : nullptr;
    if (int error = require(dest != nullptr, 7)) return error;
    if (int error = require(std::fputc('S', dest) == 'S' && std::fclose(dest) == 0, 8)) return error;
    return 0;
}
""",
        encoding="utf-8",
    )
    env = os.environ.copy()
    if toolchain == "zig":
        import importlib.util

        zig = Path(importlib.util.find_spec("ziglang").origin).with_name("zig.exe")
        env["ZIG_EXE"] = str(zig)
        generator = ["-G", "Ninja", f"-DCMAKE_TOOLCHAIN_FILE={root / 'windows/toolchain/zig-windows.cmake'}"]
        executable = scratch / "build/path_probe.exe"
    else:
        generator = ["-G", "Visual Studio 17 2022", "-A", "x64"]
        executable = scratch / "build/Release/path_probe.exe"
    subprocess.run(["cmake", "-S", str(scratch), "-B", str(scratch / "build"), *generator],
                   env=env, check=True)
    subprocess.run(["cmake", "--build", str(scratch / "build"), "--config", "Release",
                    "--parallel", "2", "--verbose"], env=env, check=True)
    directory = scratch / "\u65e5\u672c \u00e9 \U0001f680"
    directory.mkdir()
    disc = directory / "EBOOT \u5c01.ELF"
    disc.write_bytes(b"R")
    save = directory / "\u4fdd\u5b58.dat"
    installed = directory / executable.name
    shutil.copyfile(executable, installed)
    env["PS3_VFS_ROOT"] = str(disc)
    env["PS3_HDD0_HOME_ROOT"] = str(save)
    subprocess.run([str(installed), str(disc)], cwd=directory, env=env, check=True)
    assert save.read_bytes() == b"S"
    print(f"{toolchain}: Unicode argv, environment, disc read, save write and executable path passed")

"""Source-only checks of immutable contribution heads on a Windows runner."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

SDK = '5244db92669b54831c6d3ebb7a96d58c4dbfa6df'
PATCH = 'patches/ps3recomp-0001-racacit-runtime-fixes.patch'
SOURCE = '7cf3e28d65cb1336088fc601894f2e9b45078cc6'
REPO = Path(__file__).resolve().parent.parent
ROOT = Path(os.environ['RUNNER_TEMP']) / 'racit-components'


def run(*args, cwd=None, capture=False):
    print('RUN', *map(str, args), flush=True)
    return subprocess.run(list(map(str, args)), cwd=cwd, check=True,
                          text=True, capture_output=capture, timeout=1200)


def archive(repo, rev, dest):
    dest.mkdir(parents=True)
    tar = dest.with_suffix('.tar')
    run('git', '-C', repo, 'archive', '--format=tar', rev, '-o', tar)
    run('tar', '-xf', tar, '-C', dest)
    tar.unlink()


def fetch(name, url, rev):
    repo = ROOT / name
    repo.mkdir()
    run('git', 'init', repo)
    # Git owns its object cache and verifies the immutable commit identity.
    # Retry the same repository, retaining successfully transferred objects.
    for attempt in range(3):
        try:
            run('git', '-C', repo, 'fetch', '--depth=1', url, rev)
            break
        except subprocess.CalledProcessError:
            if attempt == 2:
                raise
            time.sleep(5 * (attempt + 1))
    assert run('git', '-C', repo, 'rev-parse', 'FETCH_HEAD', capture=True).stdout.strip() == rev
    tree = ROOT / (name + '-source')
    archive(repo, rev, tree)
    return tree


def variant(base, dest, touched):
    # Copy all files a patch may write. Untouched files share immutable storage.
    def copy(source, target):
        if Path(source).relative_to(base).as_posix() in touched:
            return shutil.copy2(source, target)
        os.link(source, target)
        return target
    shutil.copytree(base, dest, copy_function=copy)


def compiler(sdk, cpp=False):
    args = [ZIG, 'c++' if cpp else 'cc', '-target', 'x86_64-windows-gnu',
            '-O1', '-g', '-fno-omit-frame-pointer', '-std=c++17' if cpp else '-std=gnu11',
            '-DPS3RECOMP_PAD_USE_SDL2=1', '-DPS3RECOMP_RSX_VULKAN=1']
    dirs = ['include', 'runtime/ppu', 'runtime/platform', 'libs/video', 'libs/input',
            'libs/system', 'libs/filesystem', 'libs/audio', 'libs/network', 'libs/spurs',
            'libs/sync', 'libs/codec', 'libs/font', 'libs/misc']
    for directory in [*(sdk / d for d in dirs), SDL, VULKAN / 'include']:
        args += ['-I', directory]
    return args


def object_file(sdk, source, out, cpp=False):
    run(*compiler(sdk, cpp), '-c', source, '-o', out)


def check():
    print('EXACT HEAD', SOURCE, flush=True)
    source = ROOT / 'audio-lag-source'
    archive(REPO, SOURCE, source)
    patch = source / PATCH
    touched = {line[6:] for line in patch.read_text(encoding='utf-8').splitlines() if line.startswith('+++ b/')}
    sdk = ROOT / 'audio-lag-sdk'
    variant(BASE, sdk, touched)
    run('git', 'apply', '--ignore-whitespace', patch, cwd=sdk)
    out = ROOT / 'audio-lag-out'
    out.mkdir()
    audio = sdk / "libs/audio/cellAudio.c"
    object_file(sdk, audio, out / "audio-wasapi.o")
    run(*compiler(sdk), "-DPS3RECOMP_AUDIO_USE_SDL2=1",
        "-c", audio, "-o", out / "audio-sdl.o")
    with open(os.environ['GITHUB_STEP_SUMMARY'], 'a', encoding='utf-8') as file:
        file.write(f'PASS at `{SOURCE}`: Windows WASAPI and SDL2 audio objects.\n')


if __name__ == '__main__':
    assert sys.platform == 'win32', 'Run on actual Windows.'
    assert not ROOT.exists(), 'Fresh runner staging required.'
    ROOT.mkdir()
    import ziglang
    ZIG = Path(ziglang.__file__).parent / 'zig.exe'
    run(ZIG, 'version')
    BASE = fetch('sdk', 'https://github.com/sp00nznet/ps3recomp.git', SDK)
    sdl_source = fetch('sdl', 'https://github.com/libsdl-org/SDL.git', '5d249570393f7a37e037abf22cd6012a4cc56a71')
    SDL = ROOT / 'sdl-include'
    variant(sdl_source / 'include', SDL / 'SDL2', set())
    VULKAN = fetch('vulkan', 'https://github.com/KhronosGroup/Vulkan-Headers.git', 'c46850864f4661461b0f6cb9922c058ffea4915e')
    check()

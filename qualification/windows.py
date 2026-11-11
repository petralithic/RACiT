"""Source-only checks of immutable contribution heads on a Windows runner."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

SDK = '5244db92669b54831c6d3ebb7a96d58c4dbfa6df'
PATCH = 'patches/ps3recomp-0001-racacit-runtime-fixes.patch'
HEADS = {
    'profiles': '54b65dcc71c9dca0e349a959a76a45784ad17839',
    'stack': '34b12ccf8590872164e1e56a225db8e056890ead',
    'graphs': 'dd16ee805b0ec7d409ee6dd2468756ad8e91520d',
    'input': 'e3919cd1ef68d7c69fbb9f35f9fa9291648fba91',
    'water': '2703fc819defd88d3109ee2c9ff7191e4e7549ab',
}
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


def function(source, signature):
    assert source.count(signature) == 1
    start = source.index(signature)
    body = source.index('{', start)
    depth = 0
    for end in range(body, len(source)):
        if source[end] == '{':
            depth += 1
        elif source[end] == '}':
            depth -= 1
            if depth == 0:
                return source[start:end + 1]
    raise ValueError('unterminated actual function')


def summary(name, text):
    with open(os.environ['GITHUB_STEP_SUMMARY'], 'a', encoding='utf-8') as file:
        file.write(f'- `{name}` at `{HEADS[name]}` — {text}\n')


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


def executable(sdk, name, out, *sources, extra=()):
    exe = out / (name + '.exe')
    run(*compiler(sdk, cpp=any(str(s).endswith('.cpp') for s in sources)),
        *extra, *sources, '-o', exe)
    run(exe)


def shaders(sdk, out):
    glslang = fetch('glslang', 'https://github.com/KhronosGroup/glslang.git',
                    '2fbed5e1fb43a1c480fe020e3bb686a31010989a')
    project = ROOT / 'shader-project'
    project.mkdir()
    # Match windows/CMakeLists.txt and drive the actual runtime compiler API.
    cmake = f'''cmake_minimum_required(VERSION 3.22)
project(water_shader_check C CXX)
set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
set(ENABLE_HLSL ON CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(BUILD_EXTERNAL OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory("{glslang.as_posix()}" glslang EXCLUDE_FROM_ALL)
add_executable(depth "{(REPO / 'qualification/depth.cpp').as_posix()}" "{sdk.as_posix()}/libs/video/rsx_shader_spirv.cpp")
target_compile_features(depth PRIVATE cxx_std_17)
target_compile_options(depth PRIVATE -UNDEBUG)
target_compile_definitions(depth PRIVATE PS3RECOMP_HAVE_SPIRV_TRANSLATION=1)
target_include_directories(depth PRIVATE "{sdk.as_posix()}/include" "{sdk.as_posix()}/libs/video" "{out.as_posix()}")
target_link_libraries(depth PRIVATE glslang SPIRV glslang-default-resource-limits)
'''
    (project / 'CMakeLists.txt').write_text(cmake, encoding='utf-8')
    os.environ['ZIG_EXE'] = str(ZIG)
    toolchain = ROOT / 'water-racit/windows/toolchain/zig-windows.cmake'
    build = ROOT / 'shader-build'
    run('cmake', '-S', project, '-B', build, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
        f'-DCMAKE_TOOLCHAIN_FILE={toolchain.as_posix()}')
    run('cmake', '--build', build, '--target', 'depth', '--parallel', '2')
    run(build / 'depth.exe')


def check(name, sha):
    print('EXACT HEAD', name, sha, flush=True)
    run('git', 'cat-file', '-e', sha + '^{commit}', cwd=REPO)
    source = ROOT / (name + '-racit')
    archive(REPO, sha, source)
    if name == 'profiles':
        run(sys.executable, '-m', 'py_compile', source / 'scripts/patch_lift.py')
        invalid = ROOT / 'invalid-profile.elf'
        invalid.write_bytes(b'source-only invalid executable for rejection check\n')
        result = subprocess.run([sys.executable, str(source / 'scripts/patch_lift.py'),
                                 '--check-profile', str(invalid)], capture_output=True, text=True, timeout=30)
        diagnostics = result.stdout + result.stderr
        print(diagnostics, flush=True)
        assert result.returncode != 0 and 'Unrecognized EBOOT.ELF' in diagnostics
        summary(name, 'PASS Python compilation and real invalid-executable rejection. Valid game identification remains outside this source-only run.')
        return
    patch = source / PATCH
    touched = {line[6:] for line in patch.read_text(encoding='utf-8').splitlines() if line.startswith('+++ b/')}
    sdk = ROOT / (name + '-sdk')
    variant(BASE, sdk, touched)
    run('git', 'apply', '--check', '--ignore-whitespace', patch, cwd=sdk)
    run('git', 'apply', '--ignore-whitespace', patch, cwd=sdk)
    out = ROOT / (name + '-out')
    out.mkdir()
    fixture = REPO / 'qualification'
    if name == 'stack':
        executable(sdk, 'stack', out, fixture / 'stack.cpp')
        summary(name, 'PASS actual Windows valid/protected/cross-page/overflow reads. Generated PPU caller is outside this source-only compile.')
    elif name == 'graphs':
        object_file(sdk, sdk / 'libs/video/rsx_host_ui.c', out / 'ui.o')
        executable(sdk, 'graphs', out, fixture / 'graphs.c')
        summary(name, 'PASS SDL-enabled UI object and actual graph empty/full/wrapped-ring finite-geometry checks.')
    elif name == 'input':
        for file in ['libs/input/cellPad.c', 'libs/video/rsx_host_ui.c']:
            object_file(sdk, sdk / file, out / (Path(file).stem + '.o'))
        object_file(sdk, source / 'game/button_prompts.cpp', out / 'prompts.o', cpp=True)
        executable(sdk, 'mouse', out, fixture / 'mouse.cpp', source / 'game/desktop_mouse.cpp')
        prompts = (source / 'game/button_prompts.cpp').read_text(encoding='utf-8')
        codec = prompts[prompts.index('void dxt5_decode('):prompts.index('/* A pixel of a DXT5 picture')]
        (out / 'dxt_helpers.h').write_text(codec, encoding='utf-8')
        executable(sdk, 'atlas', out, fixture / 'atlas.cpp', extra=['-I', out, '-I', source / 'game'])
        summary(name, 'PASS actual pad/UI/prompt objects, mouse components and synthetic-atlas apply/restore checks.')
    elif name == 'water':
        for file in ['rsx_draw_engine.c', 'rsx_fp_decompiler.c']:
            object_file(sdk, sdk / 'libs/video' / file, out / (file + '.o'))
        engine = (sdk / 'libs/video/rsx_draw_engine.c').read_text(encoding='utf-8')
        test = (fixture / 'remap.c').read_text(encoding='utf-8').replace('/* ACTUAL_SIGNED_MASK */', function(engine, 'static u64 eng_signed_mask(void)'))
        (out / 'remap.c').write_text(test, encoding='utf-8')
        executable(sdk, 'remap', out, out / 'remap.c', sdk / 'libs/video/rsx_texture_layout.c')
        start = engine.index('static const char* const kEngDepthAsColorVS =')
        end = engine.index('\n\n/*', start)
        (out / 'depth_shaders.h').write_text(engine[start:end], encoding='utf-8')
        shaders(sdk, out)
        summary(name, 'PASS actual engine/FP objects, 192 remap cases and exact depth shaders through actual runtime translator/pinned Windows glslang; vertex ATTR0 interface checked.')


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
    failed = []
    for name, sha in HEADS.items():
        try:
            check(name, sha)
        except (AssertionError, OSError, ValueError, subprocess.SubprocessError) as error:
            print(f'FAILED {name} {sha}: {error}', flush=True)
            summary(name, f'FAILED {type(error).__name__}; inspect compiler/test output.')
            failed.append(name)
    print('Failed contributions:', failed, flush=True)
    print('Driver/gameplay and generated game callers remain outside this source-only run.', flush=True)
    sys.exit(bool(failed))

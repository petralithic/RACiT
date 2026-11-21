#include <stdint.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef __linux__
#include <filesystem>
#endif

#define WINDOW_WIDTH 1280
#define WINDOW_HEIGHT 720

static int initialized = 0, prepared = 0;
static int init_result = 0, prepare_result = 0;

static void require(bool ok, const char* description)
{
    if (ok) return;
    std::fprintf(stderr, "%s\n", description);
    std::exit(1);
}

static void configure(const char* key, const char* value)
{
#ifdef _WIN32
    require(_putenv_s(key, value) == 0, "Could not configure the fixture");
#else
    require(setenv(key, value, 1) == 0, "Could not configure the fixture");
#endif
}

static bool setting(const char* key, const char* expected)
{
    const char* value = getenv(key);
    return value && std::strcmp(value, expected) == 0;
}

static int rsx_vulkan_backend_init(uint32_t width, uint32_t height, const char* title)
{
    require(width == WINDOW_WIDTH && height == WINDOW_HEIGHT && title && *title,
            "Prepare should supply the normal target size and a title");
    require(setting("PS3RECOMP_VK_WINDOW", "0") && setting("RACACIT_PREPARE", "1") &&
            setting("PS3RECOMP_VK_GUEST_PROGRAMS", "1"),
            "Vulkan must start offscreen with the recorded shader program path");
    ++initialized;
    return init_result;
}

static int rsx_vulkan_prepare_shaders()
{
    ++prepared;
    return prepare_result;
}

#include "prepare_shaders_source.inc"
#ifdef __linux__
#include "prepare_shaders_linux_source.inc"
#endif

int main()
{
    char program[] = "racit", flag[] = "--prepare-shaders", game[] = "EBOOT.elf";
    char* command[] = { program, flag, nullptr };
    char* normal[] = { program, game, nullptr };
    char* launcher[] = { program, nullptr };
    configure("RACACIT_PREPARE", "0");
    require(shader_prepare_requested(2, command), "The command should enter prepare mode");
    require(!shader_prepare_requested(2, normal), "A regular game argument should enter the game");
    configure("RACACIT_PREPARE", "1");
#ifdef _WIN32
    require(shader_prepare_requested(1, launcher), "Keep the Windows launcher protocol");
#else
    require(!shader_prepare_requested(1, launcher), "Desktop POSIX should require the explicit command");
#endif

#ifdef __linux__
    for (const char* key : {"RSX_SHADER_CACHE", "RSX_PIPELINE_CACHE", "RSX_PIPELINE_LIST", "RSX_PROGRAM_LIST"})
        unsetenv(key);
    const fs::path root = fs::current_path() / "cache";
    configure("XDG_CACHE_HOME", root.c_str());
    configure("RACACIT_RSX", "null");
    linux_configure_shader_cache(false);
    require(!fs::exists(root), "A CPU-only game should skip Vulkan cache defaults");
    linux_configure_shader_cache(true);
    require(setting("RSX_PIPELINE_LIST", (root / "racit/pipelinelist.bin").c_str()),
            "An explicit preparation request should still get the Linux cache paths");
#endif
    configure("RSX_PIPELINE_LIST", "");
    configure("RSX_SHADER_CACHE", "./shadercache.bin");
    configure("RSX_PIPELINE_CACHE", "./pipelinecache.bin");
    require(prepare_shaders_offscreen() == 1 && initialized == 0 && prepared == 0,
            "A missing cache path should fail before initializing Vulkan");
    configure("RSX_PIPELINE_LIST", "./pipelinelist.bin");
    configure("PS3RECOMP_VK_WINDOW", "1");
    configure("PS3RECOMP_VK_GUEST_PROGRAMS", "0");

    init_result = -1;
    require(prepare_shaders_offscreen() == 2 && initialized == 1 && prepared == 0,
            "Vulkan initialization failure must skip compilation");
    init_result = 0;
    prepare_result = -1;
    require(prepare_shaders_offscreen() == 1 && initialized == 2 && prepared == 1,
            "Report preparation failure");
    prepare_result = 0;
    require(prepare_shaders_offscreen() == 0 && initialized == 3 && prepared == 2,
            "Report successful offscreen preparation");
    std::puts("Passed command selection, offscreen Vulkan configuration, and preparation result handling.");
}

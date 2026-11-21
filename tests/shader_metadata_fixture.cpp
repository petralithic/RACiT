#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

constexpr int VK_UUID_SIZE = 16;
struct VkPhysicalDeviceProperties {
    unsigned char pipelineCacheUUID[VK_UUID_SIZE];
    unsigned int driverVersion;
    char deviceName[32];
};
static struct { int phys; } s_vk;
static void pvkGetPhysicalDeviceProperties(int, VkPhysicalDeviceProperties* properties)
{
    *properties = {{0}, 1, "Test adapter"};
}
static void rsx_vulkan_prepare_progress(int out[5])
{
    const int reported[] = {3, 7, 7, 0, 0};
    std::memcpy(out, reported, sizeof reported);
}

#include "shader_metadata_paths.inc"

static void require(bool pass, const char* description)
{
    if (!pass) {
        std::fprintf(stderr, "%s\n", description);
        std::exit(1);
    }
}

static void cache_path(const std::string& path)
{
#ifdef _WIN32
    require(_putenv_s("RSX_PIPELINE_CACHE", path.c_str()) == 0, "set test path");
#else
    require(setenv("RSX_PIPELINE_CACHE", path.c_str(), 1) == 0, "set test path");
#endif
}

static void stamp_path(const std::string& cache, const std::string& expected)
{
    cache_path(cache);
    char path[1100];
    require(vk_cache_sibling(path, sizeof path, "shaders.stamp") == 1, "resolve stamp path");
    require(path == expected, "stamp must be a sibling of the cache file");
}

static void verify_files(const std::string& cache, const std::filesystem::path& folder)
{
    cache_path(cache);
    vk_prep_progress_write();
    vk_shaders_state("ok");
    std::string line;
    std::ifstream progress(folder / "shaders.progress");
    require(static_cast<bool>(std::getline(progress, line)) && line == "3 7 7 0 0", "progress file at cache location");
    std::ifstream state(folder / "shaders.state");
    require(static_cast<bool>(std::getline(state, line)) && line == "cache=ok", "state file at cache location");
}

int main()
{
    std::filesystem::create_directory("nested");
    stamp_path("pipelinecache.bin", "shaders.stamp");
    verify_files("pipelinecache.bin", ".");
    stamp_path("nested/pipelinecache.bin", "nested/shaders.stamp");
    verify_files("nested/pipelinecache.bin", "nested");
    const auto absolute = std::filesystem::current_path() / "nested";
    stamp_path((absolute / "pipelinecache.bin").string(), (absolute / "shaders.stamp").string());
#ifdef _WIN32
    stamp_path("C:pipelinecache.bin", "C:shaders.stamp");
    stamp_path("C:/nested\\pipelinecache.bin", "C:/nested\\shaders.stamp");
    stamp_path("\\\\server\\cache\\pipelinecache.bin", "\\\\server\\cache\\shaders.stamp");
#else
    stamp_path("folder\\pipelinecache.bin", "shaders.stamp");
#endif
    stamp_path("nested/" + std::string(1200, 'x'), "nested/shaders.stamp");
    char untouched[1100] = "guard";
    cache_path(std::string(1200, 'x') + "/pipelinecache.bin");
    require(!vk_cache_sibling(untouched, sizeof untouched, "shaders.stamp") &&
            std::strcmp(untouched, "guard") == 0, "long directory must not be truncated");
    cache_path("pipelinecache.bin");
    require(!vk_cache_sibling(untouched, 5, "shaders.stamp"), "reject short output");
    cache_path("");
    require(!vk_cache_sibling(untouched, sizeof untouched, "shaders.stamp"), "reject unset cache");
    std::puts("Progress and state files were written beside bare and nested caches. Native stamp paths and truncation checks passed.");
}

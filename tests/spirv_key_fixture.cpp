#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

using u32 = uint32_t;
using u64 = uint64_t;
static unsigned translations;

static int hlsl_to_spirv_compile(const char*, int, u32*, u32, u32*, char*, u32)
{
    ++translations;
    return -1;
}

#include "spirv_key_cache.inc"
#define make_key legacy_key
#include "spirv_legacy_key.inc"
#undef make_key

static void require(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        std::exit(1);
    }
}

using SourceSet = std::array<std::string, 32>;

static double time_hashes(Key (*key_fn)(const char*, int), const SourceSet& sources)
{
    const auto start = std::chrono::steady_clock::now();
    volatile u64 observed = 0;
    for (int iteration = 0; iteration < 64; ++iteration) {
        for (const auto& source : sources) {
            const auto key = key_fn(source.c_str(), iteration % 2);
            observed = observed ^ key.a ^ key.b;
        }
    }
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

int main()
{
    const char* path = "legacy-shadercache.bin";
#ifdef _WIN32
    require(_putenv_s("RSX_SHADER_CACHE", path) == 0, "set cache environment");
#else
    require(setenv("RSX_SHADER_CACHE", path, 1) == 0, "set cache environment");
#endif
    SourceSet sources;
    u32 state = 0x723915e1u;
    for (auto& source : sources) {
        source.resize(8192);
        for (auto& byte : source) {
            state = state * 1664525u + 1013904223u;
            byte = static_cast<char>(1u + (state >> 24) % 255u);
        }
    }
    require(make_key("", 0) == legacy_key("", 0), "empty source key changed");
    require(make_key("x", 1) == legacy_key("x", 1), "single byte key changed");
    FILE* file = std::fopen(path, "wb");
    require(file && std::fwrite(kMagic, 1, sizeof kMagic, file) == sizeof kMagic, "create legacy cache");
    for (u32 index = 0; index < sources.size(); ++index) {
        for (int stage = 0; stage < 2; ++stage) {
            const auto key = legacy_key(sources[index].c_str(), stage);
            require(make_key(sources[index].c_str(), stage) == key, "legacy key changed");
            const u32 words[] = { 0x07230203, index * 2u + static_cast<u32>(stage) };
            require(put_record(file, key, words, 2), "write legacy cache record");
        }
    }
    require(std::fclose(file) == 0, "close legacy cache");
    for (u32 index = 0; index < sources.size(); ++index) {
        for (int stage = 0; stage < 2; ++stage) {
            u32 words[2]{}, count = 0;
            require(rsx_hlsl_to_spirv(sources[index].c_str(), stage, words, 2, &count, nullptr, 0) == 0 &&
                    count == 2 && words[0] == 0x07230203 && words[1] == index * 2u + static_cast<u32>(stage),
                    "old cache record missed or changed");
        }
    }
    require(translations == 0, "warm cache caused a translation");
    if (s_file) std::fclose(s_file);
    std::printf("64 legacy keys matched and all 64 cache records were reused.\n");
    for (int pair = 0; pair < 4; ++pair) {
        double before, after;
        if (pair % 2 == 0) {
            before = time_hashes(legacy_key, sources);
            after = time_hashes(make_key, sources);
        } else {
            after = time_hashes(make_key, sources);
            before = time_hashes(legacy_key, sources);
        }
        std::printf("Hashing 16 MiB: two passes %.2f ms, fused %.2f ms\n", before, after);
    }
}

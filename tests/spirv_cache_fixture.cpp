#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <stdexcept>
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
static bool reject_replace;
static unsigned translations;

#ifdef _WIN32
static BOOL checked_replace(LPCSTR from, LPCSTR to, DWORD flags)
{
    if (!reject_replace) return MoveFileExA(from, to, flags);
    SetLastError(ERROR_ACCESS_DENIED);
    return FALSE;
}
#define MoveFileExA checked_replace
#else
static int checked_replace(const char* from, const char* to)
{
    if (!reject_replace) return rename(from, to);
    errno = EACCES;
    return -1;
}
#define rename checked_replace
#endif

static int hlsl_to_spirv_compile(const char* name, int, u32* words, u32 max, u32* out, char*, u32)
{
    if (!name || max < 2) return -1;
    ++translations;
    words[0] = 0x07230203;
    words[1] = static_cast<u32>(strlen(name));
    *out = 2;
    return 0;
}

#include "spirv_cache_source.inc"
#undef MoveFileExA
#undef rename

namespace fs = std::filesystem;

static void require(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
}

static void env(const char* name, const char* value)
{
#ifdef _WIN32
    require(_putenv_s(name, value) == 0, "Could not set environment option");
#else
    require(setenv(name, value, 1) == 0, "Could not set environment option");
#endif
}

static void close_cache()
{
    if (s_file) fclose(s_file);
    s_file = nullptr;
    s_append = s_loaded = false;
    s_hits = s_misses = 0;
    s_index.clear();
    s_cache.clear();
    translations = 0;
}

static FILE* seed(const char* name)
{
    FILE* out = fopen(name, "wb");
    require(out && fwrite(kMagic, 1, sizeof kMagic, out) == sizeof kMagic, "Could not seed a cache");
    return out;
}

static void expect_shader(const char* name, const std::vector<u32>& expected)
{
    std::vector<u32> words(expected.size());
    u32 count = 0;
    require(rsx_hlsl_to_spirv(name, 0, words.data(), static_cast<u32>(words.size()), &count, nullptr, 0) == 0
            && count == words.size() && words == expected, "Shader words differ");
}

static std::string file_bytes(const char* path)
{
    std::ifstream file(path, std::ios::binary);
    require(bool(file), "Could not read the cache");
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

int main()
{
    try {
        const char* torn = "interrupted.bin";
        const std::vector<u32> good{0x07230203, 0x12345678};
        const std::vector<u32> derived{0x07230203, 7};
        env("RSX_SHADER_CACHE", torn);
        FILE* out = seed(torn);
        for (const char* name : {"first", "second", "third"})
            require(put_record(out, make_key(name, 0), good.data(), 2), "Could not seed a record");
        const auto intact_length = cache_tell(out);
        const RecordHeader partial{make_key("partial", 0), 2, 2 ^ kWordCheck};
        require(fwrite(&partial, sizeof partial, 1, out) == 1 && fclose(out) == 0,
                "Could not write the truncated payload");
        rsx_hlsl_to_spirv_cache_open();
        require(s_append && s_index.size() == 3 && fs::file_size(torn) == static_cast<uint64_t>(intact_length),
                "The partial record was not removed without losing its neighbours");
        for (const char* name : {"first", "second", "third"}) expect_shader(name, good);
        require(translations == 0, "Warm shaders should survive a partial record without translation");
        expect_shader("partial", derived);
        require(translations == 1 && s_index.size() == 4, "The rebuilt shader should append successfully");
        close_cache();
        expect_shader("partial", derived);
        require(translations == 0, "The rebuilt shader should still be cached at restart");
        close_cache();

        const char* capped = "over-cap.bin";
        env("RSX_SHADER_CACHE", capped);
        env("RSX_SHADER_CACHE_MB", "1");
        out = seed(capped);
        std::vector<u32> large(220 * 1024 / 4, 0x07230203);
        for (const char* name : {"a", "b", "c", "d", "e"})
            require(put_record(out, make_key(name, 0), large.data(), static_cast<u32>(large.size())),
                    "Could not seed an oversized cache");
        require(fclose(out) == 0, "Could not finish the oversized cache");
        const auto before = file_bytes(capped);

        reject_replace = true;
        rsx_hlsl_to_spirv_cache_open();
        require(!s_append && s_index.size() == 5, "The existing cache should be readable after replacement fails");
        for (const char* name : {"a", "b", "c", "d", "e"}) expect_shader(name, large);
        require(translations == 0, "A failed replacement should preserve all five cached shaders");
        expect_shader("partial", derived);
        expect_shader("partial", derived);
        require(translations == 1 && file_bytes(capped) == before, "New translations must stay in memory while repair is blocked");
        close_cache();

        reject_replace = false;
        rsx_hlsl_to_spirv_cache_open();
        require(s_append && s_index.size() == 2 && fs::file_size(capped) < 512 * 1024,
                "Compaction should retry and keep the two newest records");
        expect_shader("d", large);
        expect_shader("e", large);
        require(translations == 0, "Compacted shaders should retain their contents");
        close_cache();
        std::puts("Kept 3 of 3 shaders after a torn append, 5 of 5 after a refused replacement, and 2 newest after retry.");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}

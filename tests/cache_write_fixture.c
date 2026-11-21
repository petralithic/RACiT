#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <sys/stat.h>
#endif

#define SDLCALL
#define VK_SUCCESS 0
static struct { int device; } s_vk = { 1 };
static struct { int pcache; } s_g = { 1 };
static int s_pcache_busy, s_pw_dirty, fail_read;

static int SDL_AtomicSet(int* value, int next)
{
    const int old = *value;
    *value = next;
    return old;
}

static int pvkGetPipelineCacheData(int device, int cache, size_t* size, void* out)
{
    (void)device;
    (void)cache;
    if (fail_read) return -1;
    const char bytes[] = "fresh";
    *size = sizeof bytes;
    if (out) memcpy(out, bytes, *size);
    return VK_SUCCESS;
}

#include "cache_write_source.inc"

static void require(int value, const char* description)
{
    if (value) return;
    fprintf(stderr, "%s\n", description);
    exit(1);
}

static FILE* write_cache(const char* path, const char* bytes)
{
    FILE* file = fopen(path, "wb");
    require(file != NULL, "Could not create a cache fixture");
    require(fwrite(bytes, 1, strlen(bytes) + 1, file) == strlen(bytes) + 1,
            "Could not write a cache fixture");
    return file;
}

static void expect_cache(const char* path, const char* expected)
{
    char bytes[32];
    FILE* file = fopen(path, "rb");
    require(file != NULL, "Expected the previous cache file to remain readable");
    const size_t length = fread(bytes, 1, sizeof bytes, file);
    require(fclose(file) == 0, "Could not close the cache fixture");
    require(length == strlen(expected) + 1 && memcmp(bytes, expected, length) == 0,
            "The cache did not contain the expected bytes");
}

static int short_write(FILE* file, void* data)
{
    (void)data;
    fputs("partial", file);
    return -1;
}

int main(void)
{
#ifdef _WIN32
    require(_mkdir("state") == 0, "Could not create the cache directory");
#else
    require(mkdir("state", 0700) == 0, "Could not create the cache directory");
#endif
    char path[] = "state/pipelinecache.bin";
    const char tmp[] = "state/pipelinecache.bin.tmp";
    require(fclose(write_cache(path, "previous")) == 0, "Could not close the starting cache");

    char truncated[sizeof path];
    require(vk_open_cache_tmp(path, truncated, sizeof truncated) == NULL,
            "A truncated temporary name must never open the live cache");
    require(vk_rewrite_file(path, short_write, NULL) != 0, "A partial rewrite must fail");
    expect_cache(path, "previous");

    FILE* out = write_cache(tmp, "replacement");
#ifdef _WIN32
    HANDLE held = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    require(held != INVALID_HANDLE_VALUE, "Could not hold the cache without delete sharing");
    const int result = vk_finish_cache_write(out, 1, tmp, path);
    CloseHandle(held);
#else
    require(chmod("state", 0500) == 0, "Could not block cache replacement");
    const int result = vk_finish_cache_write(out, 1, tmp, path);
    require(chmod("state", 0700) == 0, "Could not restore cache directory access");
    remove(tmp);
#endif
    require(result != 0, "Replacing the blocked cache must fail");
    expect_cache(path, "previous");

    fail_read = 1;
    s_pcache_busy = 1;
    require(vk_pcache_save_thread(path) != 0 && !s_pcache_busy && s_pw_dirty,
            "A driver read failure must leave the cache eligible for a retry");
    expect_cache(path, "previous");

    fail_read = 0;
    s_pw_dirty = 0;
    s_pcache_busy = 1;
    require(vk_pcache_save_thread(path) == 0 && !s_pcache_busy && !s_pw_dirty,
            "A successful retry should store the new cache and release the writer");
    expect_cache(path, "fresh");
    puts("Preserved the old cache on partial write, blocked replacement, and driver read failure. Retry replaced it.");
    return 0;
}

#include "store.h"
#include "platform.h"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>

const char* g_launcher_version = "shader-status-fixture";

static void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

static void write_file(const fs::path& path, const std::string& data)
{
    require(write_text_atomic(path, data), "Could not write the fixture file");
}

static void expect_status(const Store& store, int expected, const char* description)
{
    const int actual = store.shader_status();
    if (actual == expected) return;
    throw std::runtime_error(std::string(description) + " (got " + std::to_string(actual) + ")");
}

static void pipeline_list(const fs::path& path, int variants)
{
    const uint32_t header[] = { 1, 1 };
    std::string data = "RSXPLS01";
    data.append(reinterpret_cast<const char*>(header), sizeof header);
    data.append(variants, 'v');
    write_file(path, data);
}

int main(int argc, char** argv)
{
    try {
        require(argc == 2, "Expected an isolated state folder");
        Store store(fs::u8path(argv[1]));
        const fs::path build = store.builds_dir() / "v125";
        fs::create_directories(build);
        write_file(build / RACIT_GAME_EXE, "build");

        const fs::path list = store.cache() / "pipelinelist.bin";
        const fs::path state = store.cache() / "shaders.state";
        const fs::path stamp = store.cache() / "shaders.stamp";
        const fs::path cache_file = store.cache() / "pipelinecache.bin";
        const std::string identity = "build=v125\ndriver=fixture\n";
        const std::string complete = "variants=2\nbuilt=2\nfailed=0\nabsent=0\n";

        expect_status(store, Store::SH_NONE, "A fresh cache has no recorded pipelines");
        pipeline_list(list, 2);
        expect_status(store, Store::SH_NEVER, "A recorded list without a stamp needs preparation");
        write_file(stamp, identity + complete);
        write_file(state, "cache=ok\ndriver=fixture\n");
        expect_status(store, Store::SH_INCOMPLETE, "A stamp without the binary cache is incomplete");
        write_file(cache_file, "truncated");
        expect_status(store, Store::SH_INCOMPLETE, "A truncated binary cache is incomplete");
        write_file(cache_file, std::string(48, 'c'));
        expect_status(store, Store::SH_READY, "The current build, driver and list have full coverage");

        write_file(state, "cache=empty\ndriver=fixture\n");
        expect_status(store, Store::SH_INCOMPLETE, "A driver initialized with an empty cache needs preparation");
        write_file(state, "cache=ok\ndriver=different\n");
        expect_status(store, Store::SH_DRIVER, "A changed driver invalidates preparation");
        write_file(state, "cache=ok\ndriver=fixture\n");
        write_file(stamp, "build=v125-u120\ndriver=fixture\n" + complete);
        expect_status(store, Store::SH_BUILD, "A stamp from the other game version has its own identity");

        write_file(stamp, identity + "variants=2\nbuilt=1\nfailed=0\nabsent=1\n");
        expect_status(store, Store::SH_INCOMPLETE, "Unrecorded shader programs leave the cache incomplete");
        write_file(stamp, identity + "variants=2\nbuilt=1\nfailed=1\nabsent=0\n");
        expect_status(store, Store::SH_INCOMPLETE, "Failed variants leave the cache incomplete");
        write_file(stamp, identity + "variants=2x\nbuilt=2\nfailed=0\nabsent=0\n");
        expect_status(store, Store::SH_INCOMPLETE, "An invalid record count cannot mark the cache prepared");
        write_file(stamp, identity + complete);
        expect_status(store, Store::SH_READY, "Successful preparation restores readiness");

        fs::last_write_time(list, fs::last_write_time(stamp) + std::chrono::seconds(5));
        expect_status(store, Store::SH_INCOMPLETE, "A rewritten list with the same count still needs preparation");
        pipeline_list(list, 3);
        expect_status(store, Store::SH_INCOMPLETE, "A newly recorded pipeline needs preparation");
        write_file(stamp, identity + "variants=3\nbuilt=3\nfailed=0\nabsent=0\n");
        expect_status(store, Store::SH_READY, "A complete stamp for the expanded list restores readiness");
        std::cout << "Passed ready, partial, replaced list, missing cache, changed build and changed driver cases.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

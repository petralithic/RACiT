#include "store.h"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>

const char* g_launcher_version = "fixture";

static void check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

static void write_list(const fs::path& path, const std::string& records)
{
    const uint32_t header[] = {4, 0};
    std::ofstream file(path, std::ios::binary);
    file.write("RSXPLS01", 8);
    file.write(reinterpret_cast<const char*>(header), sizeof header);
    file.write(records.data(), static_cast<std::streamsize>(records.size()));
    file.close();
    check(static_cast<bool>(file), "write test pipeline list");
}

int main(int argc, char** argv)
{
    check(argc == 2, "fixture directory required");
    const fs::path root = fs::u8path(argv[1]);
    Store store(root);
    const BuildInfo disc{125, false, root / "disc", {}};
    const BuildInfo update{125, true, root / "update", {}};
    fs::create_directories(disc.dir);
    fs::create_directories(update.dir);
    check(disc.shader_tag() == "v125", "same-number disc tag");
    check(update.shader_tag() == "v125-u120", "same-number update tag");

    const auto list = store.cache() / "pipelinelist.bin";
    write_list(list, "PLAY");
    check(write_text_atomic(store.cache() / "seeded.txt", "125\n"), "legacy marker");
    write_list(disc.dir / "pipelinelist.bin", "DISC");
    write_list(update.dir / "pipelinelist.bin", "UPDT");

    store.seed_caches(disc);
    check(store.shader_variants() == 2, "disc cache must merge despite the legacy marker");
    store.seed_caches(update);
    check(store.shader_variants() == 3, "same-number update must merge separately");
    check(read_text(list, 64).substr(16) == "PLAYDISCUPDT", "shared cache must keep all records");
    check(trim(read_text(store.cache() / "seeded-disc.txt", 64)) == "125", "disc marker");
    check(trim(read_text(store.cache() / "seeded-u120.txt", 64)) == "125", "update marker");

    write_list(list, "PLAYUPDT");
    store.seed_caches(disc);
    store.seed_caches(update);
    check(store.shader_variants() == 2, "already merged caches must not restore evicted entries");

    const BuildInfo empty{126, true, root / "empty", {}};
    fs::create_directories(empty.dir);
    store.seed_caches(empty);
    check(trim(read_text(store.cache() / "seeded-u120.txt", 64)) == "125", "missing seed must not advance the marker");
    check(write_text_atomic(empty.dir / "pipelinelist.bin", "incomplete file"), "write invalid fixture");
    store.seed_caches(empty);
    check(trim(read_text(store.cache() / "seeded-u120.txt", 64)) == "125", "failed merge must not advance the marker");
    check(read_text(list, 64).substr(16) == "PLAYUPDT", "failed seed must preserve the cache");

    std::cout << "Both build variants merged once, preserved existing records, and rejected a failed seed.\n";
}

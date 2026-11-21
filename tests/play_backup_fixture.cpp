#include "store.h"

#include <chrono>
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
static int reads_before_error = -1;

BOOL WINAPI ReadFileFixture(HANDLE file, LPVOID data, DWORD bytes, LPDWORD got, LPOVERLAPPED overlapped)
{
    if (reads_before_error == 0) {
        SetLastError(ERROR_READ_FAULT);
        return FALSE;
    }
    if (reads_before_error > 0) --reads_before_error;
    return ReadFile(file, data, bytes, got, overlapped);
}
#endif

const char* g_launcher_version = "play-backup-fixture";

static void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

static fs::path backup(Store& store, const char* why)
{
    fs::path result;
    const std::string error = store.backup_saves(why, &result);
    if (!error.empty()) throw std::runtime_error(error);
    require(result.extension() == L".zip" && fs::is_regular_file(result),
            "Expected the platform's real ZIP backup");
    return result;
}

int main(int argc, char** argv)
{
    try {
        require(argc == 2, "Expected an isolated state folder");
        const fs::path root = fs::u8path(argv[1]);
        Store store(root / "launcher");
        const fs::path save = store.saves() / "BCUS98124_SAVE_2" / "GAME.SAV";
        require(write_text_atomic(save, "original"), "Could not create the synthetic save");
        const auto save_time = fs::last_write_time(save);
#ifdef _WIN32
        reads_before_error = 1;
        const std::string failed_hash = sha256_file(save);
        reads_before_error = -1;
        require(failed_hash.empty() && !sha256_file(save).empty(),
                "A failed read after the first buffer must not return a prefix hash");
#endif
        const fs::path first = backup(store, "play");
        const std::string first_hash = sha256_file(first);
        require(!first_hash.empty(), "Could not hash the first ZIP");
        fs::last_write_time(first, fs::last_write_time(first) - std::chrono::hours(24));
        const auto first_time = fs::last_write_time(first);
        require(backup(store, "play") == first && store.backup_list().size() == 1 &&
                fs::last_write_time(first) == first_time && sha256_file(first) == first_hash,
                "Unchanged saves should reuse the original ZIP without rewriting it");

        require(write_text_atomic(save, "changed!"), "Could not change the synthetic save");
        fs::last_write_time(save, save_time);
        const fs::path changed = backup(store, "play");
        require(changed != first && sha256_file(first) == first_hash && store.backup_list().size() == 2,
                "Changes with the same size and timestamp should create a distinct backup");

        require(write_text_atomic(changed, "corrupt ZIP"), "Could not damage the synthetic archive");
        const fs::path repaired = backup(store, "play");
        require(repaired != changed, "A replaced ZIP with a different hash must not be reused");
        fs::remove(repaired);
        const fs::path recovered = backup(store, "play");
        require(!sha256_file(recovered).empty(), "A deleted ZIP should be recreated");

        store.ini.put("keep_backups", "1");
        const fs::path manual = backup(store, "manual");
        require(manual != recovered && store.backup_list() == std::vector<fs::path>{manual},
                "A manual backup should take a fresh copy and survive retention");
        std::cout << "Passed warm reuse, changed bytes, missing and changed archives, manual ZIPs, and retention.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

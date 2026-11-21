#include "store.h"

#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#endif

const char* g_launcher_version = "save-restore-fixture";

static void require(bool ok, const std::string& error)
{
    if (!ok) throw std::runtime_error(error);
}

static void write_save(const fs::path& slot, const char* data)
{
    require(write_text_atomic(slot / "GAME.SAV", data), "Cannot create " + u8(slot));
}

static std::string read_save(const fs::path& slot)
{
    return read_text(slot / "GAME.SAV");
}

int main(int argc, char** argv)
{
    try {
        require(argc == 2, "Expected an isolated state folder");
        const fs::path root = fs::u8path(argv[1]);
        Store store(root / "launcher");
        const fs::path source = root / "folder";
        const fs::path one = "BCUS98124_SAVE_1";
        const fs::path two = "BCUS98124_SAVE_2";
        const fs::path other = store.saves() / "BCUS98124_SAVE_3";
        write_save(store.saves() / one, "live one");
        write_save(store.saves() / two, "live two");
        write_save(other, "untouched");
        write_save(source / one, "replacement one");
        write_save(source / two, "replacement two");
        const fs::path last = fs::directory_iterator(source)->path().filename() == one ? two : one;
        const fs::path first = last == one ? two : one;
        const fs::path stage = store.saves().parent_path() / ".save-restore";

#ifdef _WIN32
        HANDLE lock = CreateFileW((source / last / "GAME.SAV").c_str(), GENERIC_READ, 0, nullptr,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        require(lock != INVALID_HANDLE_VALUE, "Cannot lock the source save");
#else
        const fs::path denied = source / last / "unreadable";
        fs::create_directory(denied);
        require(chmod(denied.c_str(), 0) == 0, "Cannot make a source directory unreadable");
#endif
        std::string result = store.import_saves(source);
#ifdef _WIN32
        CloseHandle(lock);
#else
        require(chmod(denied.c_str(), 0700) == 0, "Cannot restore source directory permissions");
#endif
        require(!result.empty() && !fs::exists(stage) && read_save(store.saves() / one) == "live one"
                && read_save(store.saves() / two) == "live two", "A staging failure changed the original saves");

        const fs::path destination = store.saves() / last;
#ifdef _WIN32
        lock = CreateFileW(destination.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        require(lock != INVALID_HANDLE_VALUE, "Cannot lock the destination");
#else
        require(chflags(destination.c_str(), UF_IMMUTABLE) == 0, "Cannot protect the destination");
#endif
        result = store.import_saves(source);
#ifdef _WIN32
        CloseHandle(lock);
#else
        require(chflags(destination.c_str(), 0) == 0, "Cannot unprotect the destination");
#endif
        require(result.find("Could not restore " + u8(last)) != std::string::npos,
                "A blocked destination should fail during the slot swap. Result was " + result);
        require(!fs::exists(stage) && read_save(store.saves() / one) == "live one"
                && read_save(store.saves() / two) == "live two", "A swap failure left some slots changed");

        result = store.import_saves(source);
        require(result.empty(), "Folder restore failed. " + result);
        require(read_save(store.saves() / one) == "replacement one"
                && read_save(store.saves() / two) == "replacement two", "Folder restore left old data");

        const fs::path zip_source = root / "zip-source";
        write_save(zip_source / first, "from ZIP");
        const fs::path zip = root / "saves.zip";
        result = zip_create(zip_source, zip);
        require(result.empty(), "Cannot create a ZIP fixture. " + result);
        const auto expected = read_save(store.saves() / last);
        result = store.restore_backup(zip);
        require(result.empty(), "ZIP restore failed. " + result);
        require(read_save(store.saves() / first) == "from ZIP" && read_save(store.saves() / last) == expected
                && read_save(other) == "untouched" && !store.backup_list().empty() && !fs::exists(stage),
                "ZIP restore must replace only the selected slot and preserve an undo backup");
        std::cout << "Passed source read failure, slot swap rollback, folder restore, and ZIP restore.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

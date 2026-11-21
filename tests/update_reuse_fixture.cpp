#include "store.h"

#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#endif

const char* g_launcher_version = "update-reuse-fixture";

struct UpdateReuseFixture {
    static std::string prepare(Store& store) { return store.prepare_update_hdd(); }
    static fs::path stamp(const Store& store) { return store.update_preparation_stamp(); }
};

static void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

static std::string update_sfo()
{
    std::string bytes(85, '\0');
    bytes.replace(0, 4, "\0PSF", 4);
    bytes[8] = 52;    // Key table offset.
    bytes[12] = 69;   // Value table offset.
    bytes[16] = 2;    // TITLE_ID and APP_VER.
    bytes[22] = 4;    // String entries use format 0x0204.
    bytes[23] = 2;
    bytes[24] = bytes[28] = 10;
    bytes[36] = 9;    // APP_VER follows TITLE_ID.
    bytes[38] = 4;
    bytes[39] = 2;
    bytes[40] = bytes[44] = 6;
    bytes[48] = 10;   // APP_VER value offset.
    bytes.replace(52, 8, "TITLE_ID");
    bytes.replace(61, 7, "APP_VER");
    bytes.replace(69, 9, "BCUS98124");
    bytes.replace(79, 5, "01.20");
    return bytes;
}

int main(int argc, char** argv)
{
    try {
        require(argc == 2, "Expected an isolated state folder");
        const fs::path root = fs::u8path(argv[1]);
        Store store(root / "launcher");
        const fs::path input = root / "import";
        const fs::path asset = input / "USRDIR" / "patch.dat";
        require(write_text_atomic(input / "PARAM.SFO", update_sfo()) && write_text_atomic(asset, "original"),
                "Could not create the synthetic update");
        Progress progress;
        std::string summary;
        require(store.import_update(input, &progress, &summary).empty(), "Could not import the synthetic update");

        const fs::path staged = store.hdd_update() / "game" / "BCUS98124" / "USRDIR" / "patch.dat";
        const fs::path separate_home = store.hdd_update() / "home";
        require(write_text_atomic(separate_home / "save", "separate"),
                "Could not create a separate save folder");
        const std::string conflict = UpdateReuseFixture::prepare(store);
        require(conflict.find("separate save folder") != std::string::npos && !fs::exists(staged) &&
                read_text(separate_home / "save") == "separate",
                "A separate save folder should stop preparation before copying assets");
        fs::remove(separate_home / "save");
        fs::remove(separate_home);

        require(UpdateReuseFixture::prepare(store).empty() && read_text(staged) == "original",
                "First Play should stage the update");
        require(read_text(UpdateReuseFixture::stamp(store)) == "1\n" &&
                same_file_size_and_time(store.update_dir() / "USRDIR" / "patch.dat", staged),
                "Successful staging should record a reusable copy with the same metadata");

#ifdef _WIN32
        HANDLE held = CreateFileW(staged.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        require(held != INVALID_HANDLE_VALUE, "Could not hold the staged file without write sharing");
        const std::string warm = UpdateReuseFixture::prepare(store);
        CloseHandle(held);
#else
        const auto original_permissions = fs::status(staged).permissions();
        fs::permissions(staged, fs::perms::owner_read, fs::perm_options::replace);
        const std::string warm = UpdateReuseFixture::prepare(store);
        fs::permissions(staged, original_permissions, fs::perm_options::replace);
#endif
        require(warm.empty() && read_text(staged) == "original",
                "A warm Play should reuse the same file even when it cannot be rewritten");

        const auto first_time = fs::last_write_time(staged);
        require(write_text_atomic(asset, "updated!"), "Could not create changed update data");
        fs::last_write_time(asset, first_time);
        require(store.import_update(input, &progress, &summary).empty(),
                "Could not import the replacement update");
        require(!fs::exists(UpdateReuseFixture::stamp(store)) &&
                same_file_size_and_time(store.update_dir() / "USRDIR" / "patch.dat", staged),
                "Reimport should clear the staging record for a same-size, same-time replacement");
        require(UpdateReuseFixture::prepare(store).empty() && read_text(staged) == "updated!",
                "Reimport should refresh the staged bytes");
        fs::remove(staged);
        require(UpdateReuseFixture::prepare(store).empty() && read_text(staged) == "updated!",
                "A missing staged file should be copied again");
        std::cout << "Passed original staging, write-protected warm reuse, reimport, repair, and save conflict cases.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#ifdef __linux__
#include <filesystem>
#include <fstream>
#endif

static const char* ppu_vfs_root;
#include "disc_path_source.inc"
#ifdef __linux__
#include "linux_game_storage_source.inc"
#endif

static void require(bool ok, const char* message)
{
    if (ok) return;
    std::fprintf(stderr, "%s\n", message);
    std::exit(1);
}

static void set_option(const char* name, const char* value)
{
#ifdef _WIN32
    require(_putenv_s(name, value) == 0, "Could not set the fixture environment");
#else
    require(setenv(name, value, 1) == 0, "Could not set the fixture environment");
#endif
}

static void expect_root(const char* arg, const char* expected)
{
    set_option("PS3_VFS_ROOT", "");
    derive_vfs_root(arg);
    require(std::strcmp(ppu_vfs_root, expected) == 0 &&
            std::strcmp(getenv("PS3_VFS_ROOT"), expected) == 0, "Wrong derived disc root");
}

#ifdef __linux__
static void expect_option(const char* name, const fs::path& expected)
{
    const char* value = getenv(name);
    require(value && fs::path(value) == expected, name);
}

static void touch(const fs::path& path)
{
    std::ofstream file(path);
    require(static_cast<bool>(file), "Could not create a fixture file");
}

static void check_storage()
{
    const fs::path root = fs::current_path();
    const fs::path data = root / "user data";
    const fs::path profile = data / "racit";
    const fs::path disc_hdd = profile / "dev_hdd0";
    const fs::path update_hdd = profile / "dev_hdd0_u120";
    set_option("XDG_DATA_HOME", data.c_str());
    set_option("PS3_HDD0_ROOT", "");
    set_option("PS3_HDD0_HOME_ROOT", "");
    set_option("PS3_UPDATE_PARAM_SFO", "");
    set_option("PS3_VFS_ROOT", "");

    require(linux_configure_game_storage(false), "Could not configure the disc data directory");
    require(fs::is_directory(disc_hdd), "The disc data directory must exist before boot");
    expect_option("PS3_HDD0_ROOT", disc_hdd);
    fs::create_directory("other");
    fs::current_path(root / "other");
    set_option("PS3_HDD0_ROOT", "");
    require(linux_configure_game_storage(false), "Could not start the disc from a different directory");
    expect_option("PS3_HDD0_ROOT", disc_hdd);
    fs::current_path(root);

    const fs::path legacy("gamedata/dev_hdd0");
    fs::create_directories(legacy);
    set_option("XDG_DATA_HOME", (root / "fresh-data").c_str());
    set_option("PS3_HDD0_ROOT", "");
    require(linux_configure_game_storage(false), "Could not use the previous disc data directory");
    expect_option("PS3_HDD0_ROOT", legacy);
    require(!fs::exists(root / "fresh-data"), "Legacy saves should remain in their previous directory");
    set_option("XDG_DATA_HOME", data.c_str());

    set_option("PS3_HDD0_ROOT", "");
    require(!linux_configure_game_storage(true), "Update 1.20 requires the original disc root");
    fs::create_directories(root / "original" / "PS3_GAME");
    set_option("PS3_VFS_ROOT", (root / "original").c_str());
    require(!linux_configure_game_storage(true) && !fs::exists(update_hdd),
            "An uninstalled update should fail before creating a directory");

    const fs::path install = update_hdd / "game" / "BCUS98124";
    fs::create_directories(install / "USRDIR");
    touch(install / "PARAM.SFO");
    require(linux_configure_game_storage(true), "Could not use the standard update layout");
    expect_option("PS3_HDD0_ROOT", update_hdd);
    expect_option("PS3_UPDATE_PARAM_SFO", install / "PARAM.SFO");
    expect_option("PS3_HDD0_HOME_ROOT", disc_hdd / "home");

    const fs::path custom_hdd = root / "custom-update";
    const fs::path custom_sfo = root / "custom.sfo";
    fs::create_directories(custom_hdd / "game" / "BCUS98124" / "USRDIR");
    touch(custom_sfo);
    set_option("PS3_HDD0_ROOT", custom_hdd.c_str());
    set_option("PS3_UPDATE_PARAM_SFO", custom_sfo.c_str());
    set_option("PS3_HDD0_HOME_ROOT", "");
    require(linux_configure_game_storage(true), "Could not use the explicit update paths");
    expect_option("PS3_HDD0_ROOT", custom_hdd);
    expect_option("PS3_UPDATE_PARAM_SFO", custom_sfo);
    expect_option("PS3_HDD0_HOME_ROOT", fs::path());
    const fs::path custom_home = root / "saves";
    set_option("PS3_HDD0_HOME_ROOT", custom_home.c_str());
    require(linux_configure_game_storage(true), "Could not use the explicit save directory");
    expect_option("PS3_HDD0_HOME_ROOT", custom_home);
    std::puts("Passed per-user paths, legacy saves, early setup errors, and both update save modes.");
}
#endif

int main()
{
    expect_root("PS3_GAME/USRDIR/EBOOT.elf", ".");
    expect_root("./PS3_GAME/USRDIR/EBOOT.elf", ".");
    expect_root("disc with spaces/PS3_GAME/USRDIR/EBOOT.elf", "disc with spaces");
    expect_root("/PS3_GAME/USRDIR/EBOOT.elf", "/");
#ifdef _WIN32
    expect_root(R"(C:\PS3_GAME\USRDIR\EBOOT.elf)", "C:/");
    expect_root(R"(\\server\share\PS3_GAME\USRDIR\EBOOT.elf)", "//server/share");
#else
    expect_root(R"(disc\name/PS3_GAME/USRDIR/EBOOT.elf)", R"(disc\name)");
#endif
    set_option("PS3_VFS_ROOT", "explicit-disc");
    derive_vfs_root("different/PS3_GAME/USRDIR/EBOOT.elf");
    require(std::strcmp(ppu_vfs_root, "explicit-disc") == 0, "Explicit disc roots take precedence");
    std::puts("Passed relative, absolute, and explicit disc roots.");
#ifdef __linux__
    check_storage();
#endif
}

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <sys/stat.h>

namespace fs = std::filesystem;

static unsigned probes;
char g_sys_fs_root[512] = ".";

#ifdef _WIN32
static int counted_stat(const char* path, struct _stat64* out)
{
    ++probes;
    return _stat64(path, out);
}
#define stat _stat64
#define _stat64(...) counted_stat(__VA_ARGS__)
#else
static int counted_stat(const char* path, struct stat* out)
{
    ++probes;
    return stat(path, out);
}
#define stat(...) counted_stat(__VA_ARGS__)
#endif

#ifdef BASELINE
#include "disc_probes_baseline.inc"
#else
#include "disc_probes_current.inc"
#endif

#undef stat
#undef _stat64

static void require(bool pass, const std::string& what)
{
    if (!pass) throw std::runtime_error(what);
}

static void env(const char* name, const char* value)
{
#ifdef _WIN32
    require(_putenv_s(name, value) == 0, "Could not set the test root");
#else
    require(setenv(name, value, 1) == 0, "Could not set the test root");
#endif
}

static void touch(const fs::path& asset)
{
    fs::create_directories(asset.parent_path());
    std::ofstream out(asset, std::ios::binary);
    require(bool(out), "Could not create the test asset");
}

static void expect_path(const char* result, fs::path expected)
{
    require(fs::path(result).make_preferred() == expected.make_preferred(),
            std::string("Unexpected host path: ") + result);
}

static void expect_raw(const char* guest, const char* expected, unsigned count)
{
    char output[1024];
    probes = 0;
    sys_fs_translate_path(guest, output, sizeof output);
    expect_path(output, expected);
    require(probes == count, std::string("Unexpected stat count for ") + guest + ": " + std::to_string(probes));
}

int main()
{
    try {
        env("PS3_VFS_ROOT", "disc");
        touch("disc/USRDIR/DATA/primary.dat");
        touch("disc/extracted/USRDIR/DATA/primary.dat");
        touch("disc/PS3_GAME/USRDIR/DATA/primary.dat");
#ifdef BASELINE
        expect_raw("/dev_bdvd/PS3_GAME/USRDIR/DATA/primary.dat", "disc/USRDIR/DATA/primary.dat", 3);
        std::printf("Before: %u filesystem probes for one existing disc asset.\n", probes);
#else
        expect_raw("/dev_bdvd/PS3_GAME/USRDIR/DATA/primary.dat", "disc/USRDIR/DATA/primary.dat", 1);
        std::printf("After: %u filesystem probe for the same existing disc asset.\n", probes);
        touch("disc/extracted/USRDIR/DATA/extracted.dat");
        touch("disc/USRDIR/Media/audio.dat");
        touch("disc/PS3_GAME/USRDIR/DATA/original.dat");
        expect_raw("/dev_bdvd/PS3_GAME/USRDIR/DATA/extracted.dat", "disc/extracted/USRDIR/DATA/extracted.dat", 2);
        expect_raw("/Media/audio.dat", "disc/USRDIR/Media/audio.dat", 2);
        expect_raw("/dev_bdvd/PS3_GAME/USRDIR/DATA/original.dat", "disc/PS3_GAME/USRDIR/DATA/original.dat", 4);
        expect_raw("/dev_bdvd/PS3_GAME/USRDIR/DATA/missing.dat", "disc/USRDIR/DATA/missing.dat", 4);

        char flat_fallback[1024] = "disc/PS3_GAME/USRDIR/DATA/flat.dat";
        touch("disc/USRDIR/DATA/flat.dat");
        fs_normalize_sep(flat_fallback);
        probes = 0;
        ps3_vfs_ps3game_fallback(flat_fallback, sizeof flat_fallback);
        expect_path(flat_fallback, "disc/USRDIR/DATA/flat.dat");
        require(probes == 2, "Flattened-disc fallback should check the direct and alternate paths");

        char plain[1024] = "disc/readme.txt";
        probes = 0;
        ps3_vfs_ps3game_fallback(plain, sizeof plain);
        expect_path(plain, "disc/readme.txt");
        require(probes == 0, "A path with no alternate disc layout should skip stat");

        env("PS3_HDD0_HOME_ROOT", "shared-home");
        env("PS3_UPDATE_PARAM_SFO", "installed-update/PARAM.SFO");
        expect_raw("/dev_hdd0/home/00000001/savedata", "shared-home/00000001/savedata", 0);
        expect_raw("/dev_bdvd/PS3_GAME/PARAM.SFO", "installed-update/PARAM.SFO", 0);
        std::puts("Disc layouts, missing paths, update metadata and shared saves resolved as expected.");
#endif
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}

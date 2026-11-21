#include "store.h"

#include <fstream>
#include <iostream>
#include <stdexcept>

const char* g_launcher_version = "disc-elf-fixture";

static void require(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
}

static void write_file(const fs::path& path, const std::string& bytes)
{
    std::ofstream out(path, std::ios::binary);
    out.write(bytes.data(), bytes.size());
    require(static_cast<bool>(out), "Could not write a synthetic fixture file");
}

// The launcher checks the ELF class, endianness and PowerPC64 machine ID.
static std::string ps3_header(char marker)
{
    std::string bytes(64, '\0');
    bytes.replace(0, 4, "\x7f" "ELF");
    bytes[4] = 2;
    bytes[5] = 2;
    bytes[19] = 0x15;
    bytes.back() = marker;
    return bytes;
}

static std::string north_american_disc_sfo()
{
    std::string bytes(55, '\0');
    bytes.replace(0, 4, "\0PSF", 4);
    bytes[8] = 36;    // Key table offset.
    bytes[12] = 45;   // Value table offset.
    bytes[16] = 1;    // One entry.
    bytes[22] = 4;    // Entry type 0x0204, a string.
    bytes[23] = 2;
    bytes[24] = 10;   // Value length, including the terminator.
    bytes[28] = 10;   // Value capacity.
    bytes.replace(36, 8, "TITLE_ID");
    bytes.replace(45, 9, "BCUS98124");
    return bytes;
}

int main(int argc, char** argv)
{
    try {
        require(argc == 2, "Expected a temporary state directory");
        const fs::path root = fs::u8path(argv[1]);
        fs::create_directories(root);
        const fs::path usa = root / "usa.elf";
        const fs::path europe = root / "europe.elf";
        const fs::path unknown = root / "unknown.elf";
        write_file(usa, ps3_header('U'));
        write_file(europe, ps3_header('E'));
        write_file(unknown, ps3_header('X'));
        const std::string usa_hash = sha256_file(usa);
        const std::string europe_hash = sha256_file(europe);
        require(!usa_hash.empty() && !europe_hash.empty(), "Could not hash the synthetic executables");

        GameProfile profile = kRacitProfile;
        profile.discs.at(0).elf_sha256 = usa_hash.c_str();
        profile.discs.at(1).elf_sha256 = europe_hash.c_str();
        g_prof = &profile;
        Store store(root / "launcher", &profile);

        bool known = false;
        int version = 0;
        std::string id;
        require(Store::check_elf(europe, &known, &version, &id).empty() &&
                known && version == 100 && id == "BCES00511", "European ELF identification failed");
        require(Store::check_elf(unknown, &known, &version, &id).empty() &&
                !known && version == 0 && id.empty(), "Unknown PS3 ELF should remain importable");

        require(store.add_elf(europe).empty(), "Could not stage an ELF without a disc");
        const fs::path disc = root / "disc";
        const fs::path usrdir = disc / "PS3_GAME" / "USRDIR";
        const fs::path disc_elf = usrdir / "EBOOT.elf";
        fs::create_directories(usrdir);
        write_file(usrdir.parent_path() / "PARAM.SFO", north_american_disc_sfo());
        require(store.use_game_in_place(disc).empty() &&
                store.game().title_id == "BCUS98124", "North American disc identification failed");

        const std::string error = store.add_elf(store.pending_elf());
        require(error.find("BCES00511") != std::string::npos && error.find("BCUS98124") != std::string::npos,
                "The staged mismatch should identify both discs");
        require(sha256_file(store.pending_elf()) == europe_hash && !fs::exists(disc_elf),
                "A rejected import should leave both files intact");
        require(store.add_elf(usa).empty() && sha256_file(disc_elf) == usa_hash &&
                !fs::exists(store.pending_elf()), "The matching ELF should complete setup");
        require(store.add_elf(unknown).empty() && sha256_file(disc_elf) == sha256_file(unknown),
                "The launcher should still accept an unrecognized PS3 ELF after confirmation");

        fs::copy_file(europe, disc_elf, fs::copy_options::overwrite_existing);
        const fs::path build = store.builds_dir() / "v125";
        fs::create_directories(build);
        write_file(build / profile.discs.at(0).exe, "placeholder");
        void* process = nullptr;
        require(store.launch(&process) == error && !process && !fs::exists(store.hdd()),
                "Play should reject a manually installed mismatch before starting or writing saves");

        std::cout << "Passed disc identification, staged ELF preservation, matching and unknown import, and Play rejection.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

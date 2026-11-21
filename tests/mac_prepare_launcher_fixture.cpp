#include "store.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>

const char* g_launcher_version = "mac-prepare-fixture";
static constexpr char kPrepareArgument[] = "--prepare-shaders";

static void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

static std::string setting(const char* name)
{
    const char* value = getenv(name);
    return value ? value : "";
}

int main(int argc, char** argv)
{
    try {
        if (argc == 2 && std::string(argv[1]) == kPrepareArgument) {
            require(setting("RACACIT_PREPARE") == "1", "Expected a shader preparation request");
            std::cout << kPrepareArgument << '\n'
                      << setting("RACACIT_PREPARE_TAG") << '\n'
                      << setting("RSX_PROGRAM_LIST") << '\n';
            return 0;
        }
        require(argc == 4, "Expected an isolated state folder and two build folders");
        unsetenv("RSX_PROGRAM_LIST");
        const fs::path root = fs::u8path(argv[1]);
        Store store(root / "launcher");
        const fs::path disc = root / "disc";
        const fs::path usrdir = disc / "PS3_GAME" / "USRDIR";
        fs::create_directories(usrdir);
        require(write_text_atomic(usrdir / "EBOOT.elf", "\x7f" "ELF"), "Could not create a disc header");
        require(store.use_game_in_place(disc).empty() && store.game().ok, "Could not attach the fixture disc");
        require(write_text_atomic(store.log(), "previous gameplay log\n"), "Could not seed the gameplay log");
        int version = 0;
        require(store.install_build_folder(fs::u8path(argv[2]), &version, true).empty() && version == 124,
                "Could not install the old build");
        void* process = nullptr;
        require(!store.supports_shader_prepare(), "Hide preparation for a build without the command");
        require(!store.launch(&process, true).empty() && !process, "Do not launch an older game for preparation");
        require(!fs::exists(store.prepare_log()), "An unsupported build should never start a preparation log");

        require(store.install_build_folder(fs::u8path(argv[3]), &version, true).empty() && version == 125,
                "Could not install the capable build");
        require(store.supports_shader_prepare(), "Offer preparation for the currently selected build");
        const fs::path game_log = store.builds_dir() / "v125" / "log.txt";
        require(write_text_atomic(game_log, "previous build log\n"), "Could not seed the build log");
        const std::string error = store.launch(&process, true);
        require(error.empty() && process, "Could not start the macOS preparation child");
        ChildProcess* child = static_cast<ChildProcess*>(process);
        const bool ended = child_wait(child, 5000);
        if (!ended) {
            child_terminate(child, 1);
            child_wait(child, 5000);
        }
        const int code = child_exit_code(child);
        child_free(child);
        require(ended && code == 0, "The preparation child should complete successfully");
        require(read_text(store.prepare_log(), 4096) ==
                    std::string(kPrepareArgument) + "\nv125\n" + u8(store.cache() / "programlist.bin") + "\n",
                "Prepare should send the offscreen argument, selected build tag, and persistent program list to the child");
        require(store.launched_dir.empty() && !fs::exists(store.hdd()) &&
                read_text(store.log(), 4096) == "previous gameplay log\n" &&
                read_text(game_log, 4096) == "previous build log\n",
                "Prepare should preserve the install and gameplay logs and leave saves alone");
        std::cout << "Passed package capability, legacy build rejection, prepare child arguments, and gameplay log preservation.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

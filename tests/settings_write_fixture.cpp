#include "settings_ui.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#ifdef _WIN32
#include <windows.h>
#endif

enum class Failure { none, open, short_read, write, close };
static Failure failure;
static FILE* output;
static rsx_host_options s_opt;

static FILE* test_fopen(const char* path, const char* mode)
{
    if (failure == Failure::open && mode[0] == 'r') {
        errno = EACCES;
        return nullptr;
    }
    FILE* file = std::fopen(path, mode);
    if (mode[0] == 'w') output = file;
    return file;
}

static size_t test_fread(void* buffer, size_t size, size_t count, FILE* file)
{
    if (failure == Failure::short_read && count > 1) count /= 2;
    return std::fread(buffer, size, count, file);
}

static int test_ferror(FILE* file)
{
    if (file == output && failure == Failure::write) return 1;
    return std::ferror(file);
}

static int test_fclose(FILE* file)
{
    const bool failed = file == output && failure == Failure::close;
    if (file == output) output = nullptr;
    const int result = std::fclose(file);
    return failed ? EOF : result;
}

#define LOCK() ((void)0)
#define UNLOCK() ((void)0)
#define fopen test_fopen
#define fread test_fread
#define ferror test_ferror
#define fclose test_fclose
#include "settings_source.inc"
#undef fopen
#undef fread
#undef ferror
#undef fclose

static constexpr char path[] = "settings.txt";
static constexpr char temp[] = "settings.txt.tmp";
static const std::string original = "# Keep my bindings\r\nRACACIT_MOUSE_SENS=1.00\r\nRACACIT_BIND_START=Comma,Escape";

static std::string read_settings()
{
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), {});
}

static void write_settings(const std::string& text)
{
    std::ofstream file(path, std::ios::binary);
    if (!(file << text) || !file.flush()) std::exit(2);
}

static void require(bool pass, const char* description)
{
    if (!pass) {
        std::fprintf(stderr, "FAIL %s\n", description);
        std::exit(1);
    }
    std::printf("PASS %s\n", description);
}

int main()
{
#ifdef _WIN32
    if (_putenv_s("RACACIT_SETTINGS_FILE", path)) return 2;
#else
    if (setenv("RACACIT_SETTINGS_FILE", path, 1)) return 2;
#endif
    s_opt.mouse_sens = 150;
    write_settings(original);
    for (const auto fault : { Failure::open, Failure::short_read, Failure::write, Failure::close }) {
        failure = fault;
        settings_save_file();
        static const char* descriptions[] = {
            "", "unreadable original preserved", "short read preserved",
            "write error preserved original", "close error preserved original",
        };
        require(read_settings() == original && !std::filesystem::exists(temp),
                descriptions[static_cast<int>(fault)]);
    }
    failure = Failure::none;
    const std::string large(1 << 20, '#');
    write_settings(large);
    settings_save_file();
    require(read_settings() == large, "settings above the editor limit preserved");

    write_settings(original);
    settings_save_file();
    const auto saved = read_settings();
    require(saved.find("# Keep my bindings\r\nRACACIT_MOUSE_SENS=1.50\r\nRACACIT_BIND_START=Comma,Escape\r\n") == 0,
            "successful replacement kept custom bindings, comments and CRLF");
    require(!std::filesystem::exists(temp), "temporary file consumed");
    std::filesystem::remove(path);
    settings_save_file();
    require(read_settings().find("RACACIT_MOUSE_SENS=1.50\n") != std::string::npos,
            "first save created missing settings");
}

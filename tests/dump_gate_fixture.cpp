#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

static struct {
    const char* dump_path;
    uint32_t presented;
    unsigned width, height;
    int windowed, swapchain;
} s_vk;
static struct { int ready; } s_bt;
static struct { int last_draws, draws; } s_g;
static int readbacks, async_submissions, window_presents;
constexpr int VK_NULL_HANDLE = 0;
#define VK_LOG(...) std::fprintf(stderr, __VA_ARGS__)
static void vk_present_window() { ++window_presents; }
static void vk_bt_submit(int, int, int) { ++async_submissions; }

#include "dump_writer.inc"

static void present()
{
#include "dump_dispatch.inc"
    ++readbacks;
    ++s_vk.presented;
    if (s_vk.dump_path) {
        const unsigned char rgba[] = {255, 0, 0, 255, 0, 255, 0, 255};
        vk_dump_ppm(rgba);
    }
}

static void require(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        std::exit(1);
    }
}

static void env(const char* key, const char* value)
{
#ifdef _WIN32
    require(_putenv_s(key, value) == 0, "set test path");
#else
    require(setenv(key, value, 1) == 0, "set test path");
#endif
}

int main()
{
    env("PS3RECOMP_VK_DUMP_EVERY", "3");
    env("PS3RECOMP_VK_DUMP_GATE", "capture.gate");
    s_vk = {"frame_%02u.ppm", 0, 2, 1, 0, 0};
    s_bt.ready = 1;
    for (int frame = 0; frame < 120; ++frame) {
        if (frame == 4) std::ofstream("capture.gate").put('1');
        if (frame == 10) std::filesystem::remove("capture.gate");
        present();
    }
    require(readbacks == 2 && async_submissions == 118 && s_vk.presented == 120,
            "closed gate and non-sampled frames must bypass readback");
    require(!std::filesystem::exists("frame_00.ppm") && !std::filesystem::exists("frame_04.ppm"),
            "closed intervals must not create frame files");
    const std::string expected = std::string("P6\n2 1\n255\n") + std::string("\xff\x00\x00\x00\xff\x00", 6);
    for (const auto* name : {"frame_02.ppm", "frame_03.ppm"}) {
        std::ifstream file(name, std::ios::binary);
        const std::string pixels(std::istreambuf_iterator<char>(file), {});
        require(pixels == expected, "sampled frame keeps its number and RGB bytes");
    }
    s_vk.dump_path = nullptr;
    s_vk.windowed = s_vk.swapchain = 1;
    present();
    require(window_presents == 1 && readbacks == 2, "ordinary window present does not read back");
    s_vk.windowed = s_vk.swapchain = 0;
    present();
    require(readbacks == 3, "headless center pixel probe without a dump is still served");
    std::puts("120 gated frames: two readbacks and two complete captures. 118 frames submitted without readback.");
}

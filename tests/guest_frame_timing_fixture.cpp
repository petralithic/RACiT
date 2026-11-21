#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>

static unsigned g_frames_presented, backend_calls;
static unsigned long long g_rt_present_total_us;
static void rsx_backend_present() { ++backend_calls; }
static void racit_mods_frame() {}
static void InterlockedIncrement(unsigned* counter) { ++*counter; }
extern "C" void ps3_tl(char, uint32_t, uint32_t) {}

#include "present_guest_frame.inc"

using Clock = std::chrono::steady_clock;
static unsigned long long rt_now_us()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now().time_since_epoch()).count();
}

static void require(bool pass, const char* message)
{
    if (!pass) {
        std::fprintf(stderr, "%s\n", message);
        std::exit(1);
    }
}

int main(int argc, char** argv)
{
    require(argc == 2, "expected build profile");
    const bool enabled = std::strcmp(argv[1], "default") != 0;
    const char* raw = "intervals-ns.txt";
    const char* meta = "intervals-ns.txt.meta";
#ifdef _WIN32
    require(_putenv_s("RACIT_FRAME_INTERVALS", raw) == 0, "set recorder path");
#else
    require(setenv("RACIT_FRAME_INTERVALS", raw, 1) == 0, "set recorder path");
#endif
    require(static_cast<bool>(std::ofstream("intervals-ns.txt.request") << "10 0\n"), "arm recorder");
    if (!enabled) {
#ifdef RACACIT_RECORD_FRAME_INTERVALS
        require(false, "default build must omit the timing define");
#endif
        for (int i = 0; i < 3; ++i) present_guest_frame();
        require(g_frames_presented == 3 && !std::filesystem::exists(raw) && !std::filesystem::exists(meta),
                "default build has no recorder even when the request exists");
        std::puts("Default build ran guest presents without linking a recorder.");
        return 0;
    }
#ifndef RACACIT_RECORD_FRAME_INTERVALS
    require(false, "selected benchmark main.cpp did not receive the recording define");
#endif
    const auto end = Clock::now() + std::chrono::seconds(20);
    while (!std::filesystem::exists(meta) && Clock::now() < end) {
        rsx_backend_present(); // overlay refresh bypasses the guest callback
        present_guest_frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    std::ifstream interval_file(raw), metadata_file(meta);
    uint64_t nanos = 0, interval = 0;
    unsigned samples = 0;
    while (interval_file >> interval) {
        require(interval > 0, "expected positive intervals");
        nanos += interval;
        ++samples;
    }
    const std::string metadata(std::istreambuf_iterator<char>(metadata_file), {});
    require(samples > 0 && interval_file.eof() && nanos >= 10000000000ull, "complete 10-second interval window");
    require(samples + 1 == g_frames_presented && backend_calls == g_frames_presented * 2, "count guest flips only");
    require(metadata.find("\"complete\":true") != std::string::npos &&
            metadata.find("\"interval_count\":" + std::to_string(samples) + ",") != std::string::npos,
            "publish complete interval metadata with matching count");
    std::printf("%s: %u guest intervals and matching metadata. Overlay refreshes excluded.\n", argv[1], samples);
}

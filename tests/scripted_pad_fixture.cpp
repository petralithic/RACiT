#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using u16 = uint16_t;
using u32 = uint32_t;

#define PAD_BACKEND_XINPUT 0
#define PAD_BACKEND_SDL2 1
#define PAD_MAX_HOST_PORTS 1
#define PAD_POLL_INTERVAL_MS 4

enum { CELL_PAD_BTN_OFFSET_DIGITAL1, CELL_PAD_BTN_OFFSET_DIGITAL2 };
struct CellPadData { u16 button[2]; };
struct PadHostState {
    int connected, analog_lx, analog_ly, analog_rx, analog_ry;
    int trigger_l2, trigger_r2;
    u16 buttons;
    int sensor_x, sensor_y, sensor_z, sensor_g;
    int press_right, press_left, press_up, press_down;
    int press_triangle, press_circle, press_cross, press_square, press_l1, press_r1;
};

static PadHostState s_host_state[PAD_MAX_HOST_PORTS];
static int s_data_fresh[PAD_MAX_HOST_PORTS], s_racit_keyboard_prompts;
static bool physical_active, scripted_mouse;
static unsigned long long now = 100;
static unsigned long long pad_now_ms() { return now; }
static void SDL_AtomicSet(int* ptr, int val) { *ptr = val; }
static int rsx_host_ui_capturing() { return 0; }

static void pad_poll_sdl2()
{
    auto& pad = s_host_state[0];
    pad = {};
    pad.connected = 1;
    pad.analog_lx = pad.analog_ly = pad.analog_rx = pad.analog_ry = 128;
    if (physical_active) pad.analog_lx = 240;
}

static void pad_poll_kbm(int ignore_real)
{
    if (ignore_real && scripted_mouse) s_host_state[0].analog_rx = 230;
}

#include "scripted_pad_poll.inc"

static u16 poll_script()
{
    now += PAD_POLL_INTERVAL_MS;
    pad_poll_backend();
    CellPadData state{};
    auto* data = &state;
    const u32 port_no = 0;
    auto* hs = &s_host_state[port_no];
#include "scripted_pad_file.inc"
    return state.button[0] | (state.button[1] << 8);
}

static void require(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        std::exit(1);
    }
}

static void env(const char* name, const char* value)
{
#ifdef _WIN32
    require(_putenv_s(name, value) == 0, "set test mode");
#else
    require(setenv(name, value, 1) == 0, "set test mode");
#endif
}

static void enqueue(const char* commands)
{
    FILE* file = std::fopen("pad.txt", "wb");
    require(file && std::fputs(commands, file) != EOF && std::fclose(file) == 0, "submit pad input");
}

int main(int argc, char** argv)
{
    require(argc == 2, "expected one mode");
    const bool offscreen = std::strcmp(argv[1], "scripted") == 0;
    env("PAD_IGNORE_REAL", offscreen ? "1" : "0");
    env("PAD_FILE", "pad.txt");
    if (offscreen) {
        physical_active = scripted_mouse = true;
        enqueue("0x0001 1;0x4000 1\n");
        require(poll_script() == 0x0001 && pad_scripted_ok(), "mouse must not drop scripted Select");
        require(s_host_state[0].analog_rx == 230 && s_host_state[0].analog_lx == 128, "test mouse applies, real stick does not");
        require(poll_script() == 0x4000, "next scripted press must arrive without waiting");
        std::puts("Scripted mouse and scripted pad worked together with real input disabled.");
    } else {
        enqueue("0x0008 8;0x0001 2\n");
        require(poll_script() == 0x0008, "first scripted hold starts");
        physical_active = true;
        require(!poll_script() && !pad_scripted_ok(), "physical input suspends script");
        physical_active = false;
        now += 1000;
        require(!poll_script() && !pad_scripted_ok(), "takeover interval remains in effect");
        now += 20000;
        require(!poll_script() && pad_scripted_ok(), "interrupted sequence must not replay");
        enqueue("0x4000 1\n");
        require(poll_script() == 0x4000, "new script works after the quiet period");
        std::puts("Physical takeover paused the script and dropped its remaining commands.");
    }
}

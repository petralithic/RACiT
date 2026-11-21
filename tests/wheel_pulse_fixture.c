#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "cellPad.h"

typedef uint8_t Uint8;
typedef uint64_t Uint64;
typedef struct { int value; } SDL_atomic_t;
typedef struct { int kbm, mouse_sens, mouse_invert; } rsx_host_options;
typedef struct {
    u16 buttons;
    int connected, trigger_l2, trigger_r2;
    int press_cross, press_circle, press_square, press_triangle;
    int press_up, press_down, press_left, press_right, press_l1, press_r1;
    u8 analog_lx, analog_ly, analog_rx, analog_ry;
} PadHostState;

enum { SDL_INIT_VIDEO = 1, TRIANGLE_KEY = 20 };
static int captured = 1, focused = 1, enabled = 1;
static int next_wheel;
static Uint8 keyboard[512];
static Uint64 now;
static PadHostState s_host_state[1];
volatile int g_racacit_kbm_on;

static void SDL_AtomicSet(SDL_atomic_t* a, int value) { a->value = value; }
static int SDL_AtomicGet(SDL_atomic_t* a) { return a->value; }
static int SDL_WasInit(int subsystem) { return subsystem; }
static int SDL_GetKeyboardFocus(void) { return focused; }
static const Uint8* SDL_GetKeyboardState(int* count) { (void)count; return keyboard; }
static Uint64 SDL_GetTicks64(void) { return now; }
static Uint64 SDL_GetPerformanceCounter(void) { return now; }
static Uint64 SDL_GetPerformanceFrequency(void) { return 1000; }

static void rsx_host_ui_options(rsx_host_options* opt) { *opt = (rsx_host_options){enabled, 100, 0}; }
static void rsx_host_ui_poll_script(void) {}
static int rsx_host_ui_test_key(int code) { (void)code; return 0; }
static int rsx_host_ui_mouse_take(int* dx, int* dy, int* wheel, unsigned* buttons)
{
    *dx = *dy = 0;
    *buttons = 0;
    *wheel = next_wheel;
    next_wheel = 0;
    return captured;
}
void racit_mouse_feed(uint64_t ns, int x, int y, int cap, float sens, int inv, int stick, float* push)
{
    (void)ns; (void)x; (void)y; (void)cap; (void)sens; (void)inv; (void)stick;
    push[0] = push[1] = 0;
}
int racit_stick_byte(float keys, float mouse) { (void)keys; (void)mouse; return 128; }

#include "wheel_bindings.inc"
static void kb_init(int ignore_real) { (void)ignore_real; }
#include "wheel_poll.inc"

static u16 poll(Uint64 time, int wheel)
{
    now = time;
    next_wheel = wheel;
    s_host_state[0] = (PadHostState){0};
    pad_poll_kbm(0);
    return s_host_state[0].buttons;
}

int main(void)
{
    s_kbm_on = 1;
    for (int action = 0; action < KB_N; action++)
        for (int i = 0; i < KB_MAX_KEYS; i++) s_kb_keys[action][i] = -1;
    s_kb_keys[KB_CROSS][0] = 520;
    s_kb_keys[KB_SQUARE][0] = 521;
    s_kb_keys[KB_TRIANGLE][0] = TRIANGLE_KEY;

    SDL_AtomicSet(&s_racit_keyboard_prompts, 0);
    assert(poll(100, 1) == CELL_PAD_CTRL_CROSS);
    assert(racit_keyboard_prompts_active() && s_host_state[0].press_cross == 255);
#ifdef BASELINE
    assert(poll(104, 0) == 0 && poll(116, 0) == 0);
    puts("Baseline lost the wheel action after one polling interval.");
#else
    assert(poll(104, 0) == CELL_PAD_CTRL_CROSS);
    assert(poll(116, 0) == CELL_PAD_CTRL_CROSS);
    assert(poll(159, 0) == CELL_PAD_CTRL_CROSS && poll(160, 0) == 0);
    assert(poll(170, 1) == CELL_PAD_CTRL_CROSS);
    assert(poll(174, -1) == CELL_PAD_CTRL_SQUARE && poll(190, 0) == CELL_PAD_CTRL_SQUARE);
    focused = 0;
    assert(poll(194, 1) == 0);
    focused = 1;
    assert(poll(198, 0) == 0);
    assert(poll(200, 1) == CELL_PAD_CTRL_CROSS);
    captured = 0;
    assert(poll(204, 0) == 0);
    captured = 1;
    assert(poll(208, 0) == 0);
    assert(poll(210, 1) == CELL_PAD_CTRL_CROSS);
    enabled = 0;
    assert(poll(214, 0) == 0);
    enabled = 1;
    assert(poll(218, 0) == 0);

    keyboard[TRIANGLE_KEY] = 1;
    assert(poll(300, 0) == CELL_PAD_CTRL_TRIANGLE);
    assert(poll(450, 1) == (CELL_PAD_CTRL_TRIANGLE | CELL_PAD_CTRL_L1));
    assert(poll(466, 0) == (CELL_PAD_CTRL_TRIANGLE | CELL_PAD_CTRL_L1));
    assert(poll(470, -1) == (CELL_PAD_CTRL_TRIANGLE | CELL_PAD_CTRL_R1));
    assert(poll(529, 0) == (CELL_PAD_CTRL_TRIANGLE | CELL_PAD_CTRL_R1));
    assert(poll(530, 0) == CELL_PAD_CTRL_TRIANGLE);
    assert(poll(534, 1) == (CELL_PAD_CTRL_TRIANGLE | CELL_PAD_CTRL_L1));
    keyboard[TRIANGLE_KEY] = 0;
    assert(poll(538, 0) == 0);
    puts("60 ms wheel hold, direction changes, capture release, and Quick Select passed.");
#endif
}

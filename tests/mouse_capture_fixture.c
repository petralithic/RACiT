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

enum { SDL_INIT_VIDEO = 1 };
static int captured = 1, focused = 1, enabled = 1;
static int video = 1, modal, next_x, next_wheel;
static Uint8 keyboard[512];
static Uint64 now;
static PadHostState s_host_state[1];
volatile int g_racacit_kbm_on;

static void SDL_AtomicSet(SDL_atomic_t* a, int value) { a->value = value; }
static int SDL_AtomicGet(SDL_atomic_t* a) { return a->value; }
static int SDL_WasInit(int subsystem) { return video ? subsystem : 0; }
static int SDL_GetKeyboardFocus(void) { return focused; }
static const Uint8* SDL_GetKeyboardState(int* count) { (void)count; return keyboard; }
static Uint64 SDL_GetTicks64(void) { return now; }
static Uint64 SDL_GetPerformanceCounter(void) { return now; }
static Uint64 SDL_GetPerformanceFrequency(void) { return 1000; }

static void rsx_host_ui_options(rsx_host_options* opt) { *opt = (rsx_host_options){enabled, 100, 0}; }
static void rsx_host_ui_poll_script(void) {}
static int rsx_host_ui_test_key(int code) { (void)code; return 0; }
#ifndef BASELINE
static int rsx_host_ui_capturing(void) { return modal; }
#endif
static int rsx_host_ui_mouse_take(int* dx, int* dy, int* wheel, unsigned* buttons)
{
    *dx = next_x;
    *dy = 0;
    *buttons = 0;
    *wheel = next_wheel;
    next_x = next_wheel = 0;
    return captured;
}
extern int racit_camera_take(double* angles, uint64_t time);

#include "wheel_bindings.inc"
static void kb_init(int ignore_real) { (void)ignore_real; }
#include "wheel_poll.inc"

static u16 poll(Uint64 time, int motion, int wheel, int ignore_real)
{
    now = time;
    next_x = motion;
    next_wheel = wheel;
    s_host_state[0] = (PadHostState){0};
    pad_poll_kbm(ignore_real);
    return s_host_state[0].buttons;
}

static int take(Uint64 time)
{
    double angles[2];
    const int moving = racit_camera_take(angles, time * 1000000);
    assert(angles[1] == 0);
    if (moving) assert(angles[0] > 0);
    return moving;
}

int main(void)
{
    s_kbm_on = 1;
    for (int action = 0; action < KB_N; action++)
        for (int i = 0; i < KB_MAX_KEYS; i++) s_kb_keys[action][i] = -1;
    s_kb_keys[KB_CROSS][0] = 520;
    assert(!take(100));
    assert(poll(104, 20, 0, 0) == 0 && take(108));
    assert(poll(112, 20, 1, 0) == CELL_PAD_CTRL_CROSS);
    modal = 1;
#ifdef BASELINE
    assert(poll(116, 20, 0, 0) == CELL_PAD_CTRL_CROSS && take(120));
    puts("Baseline routed mouse motion to the camera while the host owned input.");
#else
    assert(poll(116, 20, 0, 0) == 0 && !take(120));
    assert(s_host_state[0].connected);
    modal = 0;
    assert(poll(124, 0, 0, 0) == 0 && !take(128));
    assert(poll(132, 20, 0, 0) == 0);
    focused = 0;
    assert(poll(136, 20, 1, 0) == 0 && !take(140));
    focused = 1;
    assert(poll(144, 20, 0, 0) == 0);
    enabled = 0;
    assert(poll(148, 20, 1, 0) == 0 && !take(152));
    enabled = 1;
    assert(poll(156, 0, 0, 0) == 0 && !take(160));
    assert(poll(164, 20, 1, 0) == CELL_PAD_CTRL_CROSS && take(168));

    focused = video = 0;
    assert(poll(172, 20, 0, 1) == 0 && take(176));
    puts("Modal, focus loss and disabled controls flushed pending mouse input. Gameplay and offscreen scripts still turned the camera.");
#endif
}

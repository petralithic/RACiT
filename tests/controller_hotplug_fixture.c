#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>

static Uint64 now_ms = 1000;
static Uint64 fixture_ticks(void) { return now_ms; }
#define SDL_GetTicks64 fixture_ticks
#include "libs/input/cellPad.c"
#undef SDL_GetTicks64

static int settings_requests;
int rsx_host_ui_capturing(void) { return 0; }
void rsx_host_ui_request_menu(void) { ++settings_requests; }
void rsx_host_ui_options(rsx_host_options* options) { memset(options, 0, sizeof(*options)); }
#ifdef _WIN32
/* COFF resolves references from the unused guest/report functions before stripping them. */
uint8_t* vm_base;
int g_resv_store_active;
uint32_t g_ww_lo, g_ww_hi;
volatile int g_racacit_kbm_on, g_racacit_ds_haptics, g_racacit_rumble_lg, g_racacit_rumble_sm;
volatile unsigned long long g_racacit_rumble_ms;
unsigned long long ps3_qpc_us(void) { abort(); }
void ps3_ww_report_inline(uint32_t address, uint64_t value, int width) { abort(); }
void ppu_resv_break_store(uint64_t address) { abort(); }
int spu_coh_is_reserved(uint32_t address) { abort(); }
void spu_lockline_lock(void) { abort(); }
void spu_lockline_unlock(void) { abort(); }
void spu_coh_notify_write(uint32_t address) { abort(); }
void rsx_host_ui_poll_script(void) { abort(); }
#endif
int touch_controls_active(void) { return 0; }
void touch_controls_set_gamepad(int connected) { (void)connected; }
int touch_controls_poll(float aspect, uint16_t* buttons, uint8_t* lx, uint8_t* ly,
                        uint8_t* rx, uint8_t* ry)
{
    (void)aspect; (void)buttons; (void)lx; (void)ly; (void)rx; (void)ry;
    return 0;
}

static void require(int pass, const char* message)
{
    if (pass) return;
    fprintf(stderr, "%s (SDL: %s)\n", message, SDL_GetError());
    exit(1);
}

static SDL_Joystick* attach(void)
{
    const int index = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER,
                                                SDL_CONTROLLER_AXIS_MAX, SDL_CONTROLLER_BUTTON_MAX, 0);
    require(index >= 0, "Could not attach a virtual controller");
    SDL_Joystick* joystick = SDL_JoystickOpen(index);
    require(joystick != NULL && SDL_IsGameController(index), "Could not open a virtual controller");
    return joystick;
}

static void detach(SDL_Joystick* joystick)
{
    const SDL_JoystickID id = SDL_JoystickInstanceID(joystick);
    for (int index = 0; index < SDL_NumJoysticks(); ++index) {
        if (SDL_JoystickGetDeviceInstanceID(index) == id) {
            require(SDL_JoystickDetachVirtual(index) == 0, "Could not detach a virtual controller");
            SDL_JoystickClose(joystick);
            return;
        }
    }
    require(0, "Could not find the controller to detach");
}

static void press(SDL_Joystick* joystick, int button, int down)
{
    require(SDL_JoystickSetVirtualButton(joystick, button, down) == 0, "Could not set a virtual button");
}

static int at_port(SDL_Joystick* joystick, int port)
{
    return s_sdl_controllers[port] && SDL_GameControllerGetJoystick(s_sdl_controllers[port]) == joystick;
}

int main(void)
{
    SDL_SetMainReady();
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    require(SDL_Init(SDL_INIT_GAMECONTROLLER) == 0, "SDL controller initialization failed");
    SDL_Joystick* first = attach();
    SDL_Joystick* next = attach();
    pad_init_backend();
    pad_poll_sdl2();
    require(at_port(first, 0) && at_port(next, 1), "Two controllers should occupy the first two ports");

    press(first, SDL_CONTROLLER_BUTTON_BACK, 1);
    press(first, SDL_CONTROLLER_BUTTON_Y, 1);
    pad_poll_sdl2();
    now_ms += 170;
    pad_poll_sdl2();
    require(g_racacit_ctx_quickselect && settings_requests == 0, "First controller should be holding its own buttons");

    detach(first);
    SDL_Joystick* replacement = attach();
    press(next, SDL_CONTROLLER_BUTTON_Y, 1);
    pad_poll_sdl2();
    require(at_port(next, 0) && at_port(replacement, 1), "Same-count replacement should join immediately");
    require(!(s_host_state[0].buttons & CELL_PAD_CTRL_SELECT) && settings_requests == 0,
            "The departing controller's Select press must not reach the game or settings");
    require(!g_racacit_ctx_quickselect, "Promoted controller must start its own quick-select hold");
    now_ms += 170;
    pad_poll_sdl2();
    require(g_racacit_ctx_quickselect, "Promoted controller's quick-select hold should still work");

    press(replacement, SDL_CONTROLLER_BUTTON_BACK, 1);
    pad_poll_sdl2();
    detach(next);
    press(replacement, SDL_CONTROLLER_BUTTON_BACK, 0);
    pad_poll_sdl2();
    require(at_port(replacement, 0) && (s_host_state[0].buttons & CELL_PAD_CTRL_SELECT),
            "Promoted controller's own Select tap should survive its port change");
    require(!g_racacit_ctx_quickselect, "Quick-select state should clear on a new primary controller");
    now_ms += PAD_SELECT_TAP_MS;
    pad_poll_sdl2();
    require(!(s_host_state[0].buttons & CELL_PAD_CTRL_SELECT), "The Select tap should expire");
    press(replacement, SDL_CONTROLLER_BUTTON_BACK, 1);
    pad_poll_sdl2();
    now_ms += PAD_SELECT_HOLD_MS;
    pad_poll_sdl2();
    require(settings_requests == 1 && !(s_host_state[0].buttons & CELL_PAD_CTRL_SELECT),
            "A long Select hold should still open Settings once");

    detach(replacement);
    pad_poll_sdl2();
    require(!s_host_state[0].connected && !g_racacit_ctx_quickselect, "Last detach should leave a neutral port");
    pad_shutdown_backend();
    SDL_Quit();
    puts("Verified Select and quick-select ownership through SDL controller handoffs.");
    return 0;
}

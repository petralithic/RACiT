#include <SDL_events.h>
#include <cstdio>
#include <stdexcept>

static int s_open, s_mcap, s_mtest, s_mdx, s_mdy, s_mwheel;
static unsigned s_mbtn, s_mbtn_tap;
static bool trophies_open;
static int trophy_move, setting_adjust;
static int s_sel;
enum { K_OPT };
static const struct { int kind, id; } k_rows[] = {{K_OPT, 7}};

#define LOCK() ((void)0)
#define UNLOCK() ((void)0)
static bool tr_shown() { return trophies_open; }
static void tr_move(int step) { trophy_move = step; }
static void adjust(int id, int step) { if (id == 7) setting_adjust = step; }

#include "mouse_take_source.inc"

static void dispatch(const SDL_Event* e)
{
    const auto handle = [](const SDL_Event* e) -> int {
        switch (e->type) {
#include "wheel_source.inc"
        default: return -1;
        }
    };
    if (handle(e) != 0) throw std::runtime_error("The wheel event was rejected");
}

static void scroll(int y, SDL_MouseWheelDirection direction)
{
    SDL_Event event{};
    event.type = SDL_MOUSEWHEEL;
    event.wheel.y = y;
    event.wheel.direction = direction;
    dispatch(&event);
}

static int take_game_wheel()
{
    int dx, dy, wheel;
    unsigned buttons;
    rsx_host_ui_mouse_take(&dx, &dy, &wheel, &buttons);
    return wheel;
}

static void require(bool ok, const char* label)
{
    if (!ok) throw std::runtime_error(label);
}

int main()
{
    try {
        s_mcap = 1;
        scroll(3, SDL_MOUSEWHEEL_NORMAL);
        require(take_game_wheel() == 3, "WheelUp must receive a normal upward event");
        scroll(-3, SDL_MOUSEWHEEL_FLIPPED);
        require(take_game_wheel() == 3, "WheelUp must receive the same physical movement in flipped mode");
        scroll(-4, SDL_MOUSEWHEEL_NORMAL);
        require(take_game_wheel() == -4, "WheelDown must receive a normal downward event");
        scroll(4, SDL_MOUSEWHEEL_FLIPPED);
        require(take_game_wheel() == -4, "WheelDown must receive the same physical movement in flipped mode");
        scroll(0, SDL_MOUSEWHEEL_FLIPPED);
        require(take_game_wheel() == 0, "A horizontal-only event must leave vertical gameplay scroll idle");
        scroll(2, SDL_MOUSEWHEEL_FLIPPED);
        scroll(1, SDL_MOUSEWHEEL_NORMAL);
        require(take_game_wheel() == -1 && take_game_wheel() == 0,
                "The pad poll must consume the combined physical scroll once");

        s_mcap = 0;
        s_open = 1;
        trophies_open = true;
        scroll(2, SDL_MOUSEWHEEL_FLIPPED);
        require(trophy_move == -3 && take_game_wheel() == 0, "The trophy list must follow system scroll");
        trophies_open = false;
        scroll(-2, SDL_MOUSEWHEEL_FLIPPED);
        require(setting_adjust == -1 && take_game_wheel() == 0, "Settings must follow system scroll");
        puts("Normal and flipped SDL events agree in captured gameplay. Menus retain system direction.");
        return 0;
    } catch (const std::exception& error) {
        fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}

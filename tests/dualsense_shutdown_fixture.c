#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#include <process.h>
typedef HANDLE thread_t;
#else
#include <pthread.h>
#include <time.h>
typedef pthread_t thread_t;
#endif
#include "ds_player.h"

#define CELL_AUDIO_BLOCK_SAMPLES 256
#define CELL_AUDIO_SAMPLE_RATE 48000
enum { SDL_INIT_AUDIO = 1, AUDIO_F32SYS, SDL_AUDIO_ALLOW_CHANNELS_CHANGE };
typedef unsigned SDL_AudioDeviceID;
typedef struct { int freq, format, channels, samples; void* callback; } SDL_AudioSpec;
typedef struct { int ds_plus, ds_speaker, ds_haptics, ds_volume; } rsx_host_options;
static int controller_present, devices, closes;
static atomic_int mixer_entered, mixer_exited;
static int slow_exit;
uint8_t* vm_base;
void (*g_audio_put_hook)(uint32_t, uint32_t, uint32_t);
static void audio_put_hook(uint32_t ea, uint32_t lsa, uint32_t size) { (void)ea; (void)lsa; (void)size; }

static int SDL_WasInit(int flags) { return flags; }
static int SDL_InitSubSystem(int flags) { (void)flags; return 0; }
static const char* SDL_GetError(void) { return "synthetic SDL"; }
static int SDL_GetNumAudioDevices(int capture) { assert(!capture); return controller_present; }
static const char* SDL_GetAudioDeviceName(int index, int capture)
{
    assert(index == 0 && !capture && controller_present);
    return "Wireless Controller";
}
static void rsx_host_ui_options(rsx_host_options* options) { *options = (rsx_host_options){1, 1, 1, 50}; }
static SDL_AudioDeviceID SDL_OpenAudioDevice(const char* name, int capture, const SDL_AudioSpec* want,
                                            SDL_AudioSpec* have, int flags)
{
    assert(!capture);
    const unsigned id = name ? 2 : 1;
    assert(!name || (controller_present && flags == SDL_AUDIO_ALLOW_CHANNELS_CHANGE));
    assert(!(devices & id));
    devices |= id;
    *have = *want;
    return id;
}
static void SDL_PauseAudioDevice(SDL_AudioDeviceID id, int pause) { assert((devices & id) && !pause); }
static void SDL_CloseAudioDevice(SDL_AudioDeviceID id)
{
    assert(mixer_exited && (devices & id));
    devices &= ~id;
    closes++;
}

#include "mixer_state.inc"
#include "device_state.inc"

static void delay_ms(unsigned ms)
{
#ifdef _WIN32
    Sleep(ms);
#else
    struct timespec request = { ms / 1000, (long)(ms % 1000) * 1000000 };
    nanosleep(&request, NULL);
#endif
}

#ifdef _WIN32
static unsigned __stdcall audio_mix_thread_func(void* arg)
#else
static void* audio_mix_thread_func(void* arg)
#endif
{
    (void)arg;
    mixer_entered = 1;
    while (s_mix_thread_running) delay_ms(1);
    if (slow_exit) delay_ms(2200);
    mixer_exited = 1;
    return 0;
}

#include "mixer_lifecycle.inc"

static void cycle(int has_controller)
{
    controller_present = has_controller;
    mixer_entered = mixer_exited = 0;
    assert(audio_backend_init() == 0);
    assert(ds_player_enabled() && s_ds_raw && devices == (has_controller ? 3 : 1));
    assert(g_racacit_ds_haptics == has_controller);
    float speaker[4] = {0}, haptics[4] = {0};
    ds_player_render(speaker, haptics, 4);
    for (unsigned i = 0; i < 4; i++) assert(speaker[i] == 0 && haptics[i] == 0);
    uint8_t sound[16] = { 0x04, 0x01, 0x77, 0x77, 0x77, 0x77 };
    ds_player_adpcm(sound, sizeof sound, CELL_AUDIO_SAMPLE_RATE, 1, 1, 1, 0, "dummy");
    ds_player_render(speaker, haptics, 4);
    assert(speaker[0] > 0 && haptics[0] > 0);
    assert(fputs("capture", s_ds_raw) >= 0);
    assert(audio_start_mix_thread() == 0);
    while (!mixer_entered) delay_ms(1);
    audio_stop_mix_thread();
    audio_backend_shutdown();
    assert(mixer_exited && !devices && !s_ds_dev && !s_ds_raw && !s_sdl_audio_dev);
    assert(!g_racacit_ds_haptics && !s_ds_haptic_dev && !ds_player_enabled());
    assert(!ds_player_line(sound, sizeof sound, CELL_AUDIO_SAMPLE_RATE, 1, 0, "disabled"));
    char capture[8] = {0};
    FILE* file = fopen("dual.raw", "rb");
    assert(file && fread(capture, 1, 7, file) == 7);
    assert(!strcmp(capture, "capture") && fgetc(file) == EOF);
    assert(fclose(file) == 0);
}

int main(void)
{
    slow_exit = 1;
    cycle(1);
    slow_exit = 0;
    cycle(0);
    cycle(1);
    assert(closes == 5);
    puts("Mixer joined, device and capture released, no stale voice, and haptics routed after reopen.");
    return 0;
}

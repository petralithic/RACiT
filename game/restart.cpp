/* The pause menu's Restart item (a prepared menu archive, RACACIT_MENU_ARCHIVE).
 *
 * The archive's movie calls fscommand("RCCITRestart"). The game's FSCommand
 * dispatcher (func_004A27F8 on disc 1.00, func_004A2DF4 on update 1.20,
 * hooked at its head by scripts/patch_lift.py and the 1.20 hook recipe) hands
 * it here first; any other command goes on to the game as before.
 *
 * A restart is a request: no process exits while a save or load is being
 * written. cellSaveData's savedata_execute admits each transaction through
 * racit_restart_save_begin / _end, and once the request is set no new one is
 * admitted. The frame clock (game/main.cpp) polls racit_restart_ready, writes
 * the settings and leaves with RACIT_RESTART_EXIT_STATUS (75); a launcher that
 * sees 75 starts the game again. On macOS SIGTERM takes the same path and
 * exits 0, so a terminated game also finishes its save first. */
#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "../tools/ps3recomp/runtime/ppu/ppu_context.h"
extern "C" uint8_t* vm_base;
#include "../tools/ps3recomp/runtime/ppu/ppu_stack_read.h"

enum { RACIT_RESTART_EXIT_STATUS = 75 };

/* One word: the in-flight save/load count in the low bits, the request flags
 * above. A single compare-and-swap admits a save only while no request is set. */
static constexpr unsigned kRestartRequested = 1u << 31;
static constexpr unsigned kTerminationRequested = 1u << 30;
static std::atomic<unsigned> s_restart_state{0};
static_assert(std::atomic<unsigned>::is_always_lock_free, "SIGTERM needs a lock-free exit state");

extern "C" void racit_restart_request(void)
{
    s_restart_state.fetch_or(kRestartRequested, std::memory_order_acq_rel);
}

extern "C" int racit_restart_save_begin(void)
{
    unsigned state = s_restart_state.load(std::memory_order_acquire);
    while (!(state & kRestartRequested))
        if (s_restart_state.compare_exchange_weak(state, state + 1, std::memory_order_acquire,
                                                  std::memory_order_relaxed))
            return 1;
    return 0;
}

extern "C" void racit_restart_save_end(void)
{
    s_restart_state.fetch_sub(1, std::memory_order_release);
}

/* Requested, and no save or load in flight. */
extern "C" int racit_restart_ready(void)
{
    return (s_restart_state.load(std::memory_order_acquire) & ~kTerminationRequested) == kRestartRequested;
}

extern "C" int racit_restart_exit_status(void)
{
    return (s_restart_state.load(std::memory_order_acquire) & kTerminationRequested) ? 0
                                                                                    : RACIT_RESTART_EXIT_STATUS;
}

#ifdef __APPLE__
static void racit_restart_on_sigterm(int)
{
    s_restart_state.fetch_or(kRestartRequested | kTerminationRequested, std::memory_order_relaxed);
}
#endif

extern "C" void racit_restart_install_sigterm_handler(void)
{
#ifdef __APPLE__
    struct sigaction action;
    std::memset(&action, 0, sizeof action);
    action.sa_handler = racit_restart_on_sigterm;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    if (sigaction(SIGTERM, &action, nullptr) != 0) std::perror("[racit] SIGTERM handler");
#endif
}

/* The dispatcher's head: r5 is the command string. Scaleform may allocate it
 * anywhere in guest memory, so it is read through the protected reader; the
 * complete spelling including the NUL must match. Returns 1 when handled (r3
 * then holds the handler's "handled" result and the dispatcher returns). */
extern "C" int racacit_restart_fscommand(ppu_context* ctx)
{
    static const char command[] = "RCCITRestart";
    const uint32_t address = (uint32_t)ctx->gpr[5];
    if ((uint64_t)address + sizeof command > 0x100000000ULL) return 0;
    /* The last word overlaps the one before so no read goes past the NUL. */
    static const unsigned offsets[] = { 0, 4, 8, sizeof command - sizeof(uint32_t) };
    for (unsigned offset : offsets) {
        uint32_t word;
        if (!racit_stack_read(address + offset, &word) || std::memcmp(&word, command + offset, sizeof word) != 0)
            return 0;
    }
    std::fprintf(stderr, "[racit] Restart requested from the menu; restarting once saves finish\n");
    racit_restart_request();
    ctx->gpr[3] = 1;
    return 1;
}

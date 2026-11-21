#include "win32_compat.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

static void (*g_gcm_wake_walker)(void);
static DWORD fixed_sleep_ms, wake_wait_ms, wake_result;
static int pending_work;
extern "C" int racacit_few_cpus(void) { return 0; }
static int cellGcm_fifo_pending() { return pending_work; }
static void record_sleep(DWORD ms) { fixed_sleep_ms = ms; }
static DWORD record_wait(HANDLE handle, DWORD ms)
{
    wake_wait_ms = ms;
    wake_result = WaitForSingleObject(handle, ms);
    return wake_result;
}

#include "graphics_wake_decls.inc"

static void initialize()
{
#include "graphics_wake_init.inc"
}

static void wait_for_work()
{
    constexpr DWORD s_poll_ms = 1;
#define Sleep record_sleep
#define WaitForSingleObject record_wait
#include "graphics_wake_wait.inc"
#undef WaitForSingleObject
#undef Sleep
}

static void require(bool pass, const char* message)
{
    if (!pass) {
        std::fprintf(stderr, "%s\n", message);
        std::exit(1);
    }
}

static void env(const char* key, const char* value)
{
#ifdef _WIN32
    require(_putenv_s(key, value) == 0, "set test mode");
#else
    const int error = *value ? setenv(key, value, 1) : unsetenv(key);
    require(error == 0, "set test mode");
#endif
}

int main(int argc, char** argv)
{
    require(argc == 2, "expected a mode");
    const char* mode = argv[1];
    const bool enabled = std::strcmp(mode, "enabled") == 0;
    const bool disabled = std::strcmp(mode, "disabled") == 0;
    env("RACACIT_WALKER_WAKE", enabled ? "1" : disabled ? "0" : "");
    env("RACACIT_WALKER_SPIN_US", "0");
#if defined(_WIN32)
    const bool expect_event = true;
#elif defined(__APPLE__)
    const bool expect_event = !disabled;
#else
    const bool expect_event = enabled;
#endif
    initialize();
    require((s_walker_wake != nullptr) == expect_event && (g_gcm_wake_walker != nullptr) == expect_event,
            "selected platform wake mode");
    if (expect_event) {
        g_gcm_wake_walker();
        wait_for_work();
        require(wake_wait_ms == 1 && wake_result == WAIT_OBJECT_0 && fixed_sleep_ms == 0, "guest signal releases idle worker");
        require(WaitForSingleObject(s_walker_wake, 0) == WAIT_TIMEOUT, "worker consumed auto-reset event");
        pending_work = 1;
        wake_wait_ms = 0;
        wait_for_work();
        require(wake_wait_ms == 0 && fixed_sleep_ms == 0, "queued work bypasses sleep");
        require(CloseHandle(s_walker_wake) != 0, "close test event");
        std::printf("%s: guest wake delivered and event reset. Pending work bypassed wait.\n", mode);
    } else {
        wait_for_work();
        require(fixed_sleep_ms == 4 && wake_wait_ms == 0, "fixed path sleeps for 4 ms");
        std::printf("%s: existing 4 ms poll selected.\n", mode);
    }
}

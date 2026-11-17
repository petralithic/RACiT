/* Mouse turning of the third-person follow camera.
 *
 * The pad thread (tools/ps3recomp libs/input/cellPad.c, pad_poll_kbm) adds the
 * captured mouse's motion here; the follow camera's update (func_000459D4, hooked
 * by scripts/patch_lift.py) takes it as yaw/pitch angles, adds them to the
 * game's own stick-driven turn and keeps the game's pitch limits. Each move is
 * used once, at full resolution and without the right stick's speed cap.
 *
 * While the hook has not run lately (another camera, a menu, a pause, or an
 * executable without the hook), racit_camera_feed returns 0 and the mouse
 * stays the right-stick emulation it was. */
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {
std::atomic<bool> captured{false};
std::atomic<int64_t> yaw_nrad{0}, pitch_nrad{0};   /* pending turn, nanoradians */
std::atomic<int64_t> follow_ns{0};                  /* last follow-camera update */

int64_t now_ns()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

/* Longer than a slow frame, short enough that motion made in a menu or another
 * camera is dropped instead of turning the camera when play resumes. */
constexpr int64_t kFollowWindowNs = 250000000;

bool follow_active(int64_t now)
{
    const int64_t last = follow_ns.load();
    return last != 0 && now - last < kFollowWindowNs;
}
}

/* Pad thread, every poll. dx/dy: the captured mouse's motion in counts since the
 * last poll. sensitivity: RACACIT_MOUSE_SENS in hundredths (100 = 1.0).
 * Returns 1 while the follow camera takes the mouse (the caller then leaves the
 * right stick alone), 0 when the mouse should drive the stick emulation. */
extern "C" int racit_camera_feed(int dx, int dy, int capture, int sensitivity, int invert_y)
{
    if (!capture || !follow_active(now_ns())) {
        captured.store(false);
        yaw_nrad.store(0);
        pitch_nrad.store(0);
        return 0;
    }
    /* 0.003 rad (about 0.17 degrees) per count at speed 1.0. */
    const double gain = 3000000.0 * sensitivity / 100.0;
    yaw_nrad.fetch_add((int64_t)(dx * gain));
    pitch_nrad.fetch_add((int64_t)(dy * gain * (invert_y ? -1 : 1)));
    captured.store(true);
    return 1;
}

/* Follow-camera update (guest thread). angles[0] = yaw, angles[1] = pitch, in
 * radians, consumed. Returns 0 when there is nothing to add, so the game's own
 * values pass through unchanged. */
/* LOCAL test: RACIT_TEST_MOUSE=<file> holding "id dx dy n": a new id feeds
 * dx/dy counts on each of the next n camera updates. */
static double t_took_yaw, t_took_pitch;
static void racit_test_mouse(void)
{
    static const char* path = getenv("RACIT_TEST_MOUSE");
    static int last_id = 0, left = 0, tdx, tdy; static unsigned calls;
    if (!path) return;
    calls++;
    FILE* f = fopen(path, "r"); int id = 0, a = 0, b = 0, n = 0;
    if (f) { if (fscanf(f, "%d %d %d %d", &id, &a, &b, &n) != 4) id = 0; fclose(f); }
    if (id && id != last_id) {
        last_id = id; tdx = a; tdy = b; left = n;
        fprintf(stderr, "[mousecam-test] call %u burst %d: %d %d x %d; consumed so far yaw %.4f pitch %.4f\n", calls, id, a, b, n, t_took_yaw, t_took_pitch);
    }
    if (left > 0) { left--; racit_camera_feed(tdx, tdy, 1, 100, 0);
        if (!left) fprintf(stderr, "[mousecam-test] call %u burst %d done; consumed yaw %.4f pitch %.4f\n", calls, last_id, t_took_yaw, t_took_pitch); }
}

extern "C" int racit_camera_take(double* angles)
{
    follow_ns.store(now_ns());
    racit_test_mouse();
    angles[0] = angles[1] = 0.0;
    if (!captured.load()) return 0;
    angles[0] = (double)yaw_nrad.exchange(0) / 1e9;
    angles[1] = (double)pitch_nrad.exchange(0) / 1e9;
    t_took_yaw += angles[0]; t_took_pitch += angles[1];
    return angles[0] != 0.0 || angles[1] != 0.0;
}

#include <cassert>
#include <cstdint>
#include <cstdio>
extern "C" void racit_camera_feed(uint64_t, int, int, int, float, int);
extern "C" int racit_camera_take(double*, uint64_t);
extern "C" int racit_camera_follow_active(uint64_t);
struct RacitMouseState { uint64_t epoch_ns; int64_t dx, dy; float x, y; };
extern "C" void racit_mouse_step(RacitMouseState*, uint64_t, int, int, int, float, int);
int main()
{
    double angles[2]; const uint64_t now = 1000000;
    racit_camera_feed(now, 0, 0, 0, 100, 0); racit_camera_take(angles, now);
    for (int i = 0; i < 100; ++i) racit_camera_feed(now, 10, -5, 1, 100, 0);
    assert(racit_camera_take(angles, now) == 1 && angles[0] == 3.0 && angles[1] == -1.5);
    racit_camera_take(angles, now); assert(angles[0] == 0 && angles[1] == 0);
    racit_camera_feed(now, 999, 999, 1, 100, 0); racit_camera_feed(now, 0, 0, 0, 100, 0);
    assert(!racit_camera_take(angles, now));
    assert(racit_camera_follow_active(now + 200000000));
    assert(!racit_camera_follow_active(now + 1001000000));
    RacitMouseState sparse{}, dense{};
    racit_mouse_step(&sparse, now, 12, -4, 1, 100, 0);
    racit_mouse_step(&dense, now, 12, -4, 1, 100, 0);
    for (uint64_t t = 2000000; t < 9000000; t += 1000000)
        racit_mouse_step(&dense, t, 0, 0, 1, 100, 0);
    racit_mouse_step(&sparse, 9000000, 0, 0, 1, 100, 0);
    racit_mouse_step(&dense, 9000000, 0, 0, 1, 100, 0);
    assert(sparse.x == dense.x && sparse.y == dense.y && dense.x > .22 && dense.y < 0);
    puts("Actual mouse consume-once/capture/follow-timeout/poll regressions passed.");
}

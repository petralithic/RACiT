#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

typedef unsigned int u32;
typedef struct { int buf; void* ptr; } vk_buffer;
static struct { int ready; } s_bt;
static struct { vk_buffer b; char path[512]; u32 w, h; int pending; } s_pp_shot;
static unsigned char rgba[] = {11, 22, 33, 255};
static int wait_error, invalidate_error, waits, invalidations, releases;

static int vk_bt_wait_all(void) { waits++; return wait_error; }
static int vk_invalidate(vk_buffer* b)
{
    assert(b == &s_pp_shot.b && b->buf);
    invalidations++;
    return invalidate_error;
}
static void vk_destroy_buffer(vk_buffer* b) { releases++; *b = (vk_buffer){0}; }

#define VK_LOG(...) fprintf(stderr, __VA_ARGS__)
#include "readback.inc"

static void record_frame(void)
{
    s_pp_shot.b = (vk_buffer){1, rgba};
    s_pp_shot.w = s_pp_shot.h = 1;
    s_pp_shot.pending = 1;
    snprintf(s_pp_shot.path, sizeof s_pp_shot.path, "frame.ppm");
    waits = invalidations = releases = 0;
}

static void expect_no_frame(void)
{
    assert(fopen(s_pp_shot.path, "rb") == NULL);
}

int main(void)
{
    s_bt.ready = 1;
    record_frame();
    wait_error = -1;
    vk_pp_flush_shot(1);
    assert(waits == 1 && invalidations == 0 && releases == 1);
    expect_no_frame();
    puts("Failed batch wait rejects the frame before reading pixels and passes the buffer to deferred release.");

    wait_error = 0;
    record_frame();
    vk_pp_flush_shot(0);
    assert(waits == 0 && invalidations == 0 && releases == 0);
    assert(s_pp_shot.b.buf && !s_pp_shot.pending);
    expect_no_frame();
    vk_pp_flush_shot(1);
    assert(waits == 1 && invalidations == 0 && releases == 1);
    expect_no_frame();

    s_bt.ready = 0;
    record_frame();
    vk_pp_flush_shot(0);
    assert(waits == 0 && invalidations == 0 && releases == 0 && s_pp_shot.b.buf);
    vk_pp_flush_shot(1);
    assert(waits == 0 && invalidations == 0 && releases == 1);
    expect_no_frame();
    puts("Buffers from failed submission or sync wait are retained, then released without publishing stale pixels.");

    record_frame();
    invalidate_error = -1;
    vk_pp_flush_shot(1);
    assert(waits == 0 && invalidations == 1 && releases == 1);
    expect_no_frame();

    invalidate_error = 0;
    record_frame();
    s_bt.ready = 1;
    vk_pp_flush_shot(1);
    assert(waits == 1 && invalidations == 1 && releases == 1);
    FILE* f = fopen(s_pp_shot.path, "rb");
    assert(f);
    const unsigned char expected[] = "P6\n1 1\n255\n\013\026\041";
    unsigned char actual[sizeof expected];
    assert(fread(actual, 1, sizeof actual, f) == sizeof expected - 1);
    for (size_t i = 0; i < sizeof expected - 1; i++) assert(actual[i] == expected[i]);
    fclose(f);
    assert(remove(s_pp_shot.path) == 0);
    puts("A valid capture after an invalidation failure writes exactly the expected PPM bytes.");
    return 0;
}

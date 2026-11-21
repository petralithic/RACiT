#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint32_t u32;
typedef struct { float x, y, width, height, minDepth, maxDepth; } VkViewport;
typedef struct {
    struct { int32_t x, y; } offset;
    struct { u32 width, height; } extent;
} VkRect2D;
enum { VK_STENCIL_FACE_FRONT_AND_BACK = 3 };
static float s_rs = 1;
static struct { unsigned cmd; } s_vk;
static struct { unsigned epoch; } s_bt = {1};
static int s_vk_foreign_bind;
static struct {
    u32 dyn_epoch, reuse_epoch, dyn_w, dyn_h, dyn_ref, dyn_have_vp, dyn_have_sc;
    float fb_sc, dyn_fbsc, dbias_units, dbias_factor, dyn_dbu, dyn_dbf;
    u32 stencil_ref, sc[4], dyn_sc[4];
    float vp[4], dyn_vp[4];
    int have_vp, have_sc;
} s_e2;

static unsigned viewports, scissors, references, biases;
static VkViewport viewport;
static VkRect2D scissor;
static u32 reference;
static float bias_units, bias_factor;

static void pvkCmdSetViewport(unsigned cmd, u32 first, u32 count, const VkViewport* settings)
{
    assert(cmd == s_vk.cmd && first == 0 && count == 1);
    viewport = *settings;
    viewports++;
}
static void pvkCmdSetScissor(unsigned cmd, u32 first, u32 count, const VkRect2D* settings)
{
    assert(cmd == s_vk.cmd && first == 0 && count == 1);
    scissor = *settings;
    scissors++;
}
static void pvkCmdSetStencilReference(unsigned cmd, u32 faces, u32 value)
{
    assert(cmd == s_vk.cmd && faces == VK_STENCIL_FACE_FRONT_AND_BACK);
    reference = value;
    references++;
}
static void pvkCmdSetDepthBias(unsigned cmd, float units, float clamp, float factor)
{
    assert(cmd == s_vk.cmd && clamp == 0);
    bias_units = units;
    bias_factor = factor;
    biases++;
}

#include "dynamic_state.inc"

static void draw(u32 w, u32 h)
{
#include "batch_dynamic.inc"
}

int main(void)
{
    s_e2.have_vp = s_e2.have_sc = 1;
    s_e2.vp[0] = 2; s_e2.vp[1] = 4; s_e2.vp[2] = 160; s_e2.vp[3] = 90;
    s_e2.sc[2] = 64; s_e2.sc[3] = 60;
    s_e2.stencil_ref = 0x17f;
    s_e2.dbias_units = 2; s_e2.dbias_factor = 4;
    for (int j = 0; j < 100; j++) draw(160, 90);
    assert(viewports == 1 && scissors == 1 && references == 1 && biases == 1);
    assert(viewport.x == 2 && viewport.y == 4 && viewport.width == 160 && viewport.height == 90);
    assert(reference == 127 && bias_units == 2 && bias_factor == 4);
    for (u32 x = 1; x <= 100; x++) {
        s_e2.sc[0] = x;
        draw(160, 90);
        assert(scissor.offset.x == (int32_t)x && scissor.offset.y == 0);
        assert(scissor.extent.width == (x > 96 ? 160 - x : 64) && scissor.extent.height == 60);
        assert(viewport.width == 160 && reference == 127 && bias_factor == 4);
    }
    unsigned total = viewports + scissors + references + biases;
#ifdef BASELINE
    assert(total == 404);
    printf("Baseline commands through 100 scissor changes: %u\n", total);
#else
    assert(total == 104);
    printf("Candidate commands through 100 scissor changes: %u\n", total);
    s_e2.vp[2] = 120;
    draw(160, 90);
    assert(viewports == 2 && scissors == 101 && references == 1 && biases == 1 && viewport.width == 120);
    s_e2.stencil_ref = 0x27f;
    draw(160, 90);
    assert(references == 1);
    s_e2.stencil_ref = 0x203;
    draw(160, 90);
    assert(references == 2 && reference == 3);
    s_e2.dbias_factor = 6;
    draw(160, 90);
    assert(viewports == 2 && scissors == 101 && biases == 2 && bias_factor == 6);
    s_e2.fb_sc = 0.5f;
    draw(160, 90);
    assert(viewports == 3 && scissors == 102 && biases == 3 && references == 2);
    assert(viewport.width == 60 && scissor.offset.x == 50 && scissor.extent.width == 32 && bias_factor == 3);
    draw(75, 45);
    assert(scissors == 103 && scissor.extent.width == 25 && scissor.extent.height == 30);
    assert(biases == 3 && references == 2);
    s_e2.fb_sc = 0;
    draw(160, 90);
    s_rs = 2;
    draw(160, 90);
    assert(viewport.width == 240 && scissor.offset.x == 160 && scissor.extent.width == 0 && bias_factor == 12);
    ++s_bt.epoch;
    total = viewports + scissors + references + biases;
    draw(160, 90);
    assert(viewports + scissors + references + biases == total + 4);
    s_vk_foreign_bind = 1;
    draw(160, 90);
    assert(viewports + scissors + references + biases == total + 8);
    assert(!s_vk_foreign_bind);
    puts("Independent state changes, scaling, clipping, batch restart and foreign draws passed.");
#endif
}

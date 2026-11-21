#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "rsx_vertex_compact.h"
#include "rsx_vp_decompiler.h"
#include "layout-constructor.inc"

static struct { u32 vp_cache_gen, vp_cache_start; } g;
static int available = 1, exact = 1, result = 1;
static u32 shader_mask;
static unsigned builds;

static int eng_guest_programs(const u8** program, u32* instructions, void* fp, void* fp_size)
{
    assert(!fp && !fp_size);
    *program = (const u8*)&shader_mask;
    *instructions = 1;
    return available;
}

int rsx_vp_analyze_inputs(const u8* program, u32 bytes, rsx_vp_input_analysis* analysis)
{
    assert(program == (const u8*)&shader_mask && bytes == 16);
    *analysis = (rsx_vp_input_analysis){shader_mask, exact};
    return result;
}

static void counted_layout(rsx_vertex_layout_plan* plan, u32 mask)
{
    builds++;
    rsx_vertex_layout_plan_init(plan, mask);
}

#define rsx_vertex_layout_plan_init counted_layout
#define eng_vertex_layout baseline_layout
#include "baseline-layout.inc"
#undef eng_vertex_layout
#include "candidate-layout.inc"
#undef rsx_vertex_layout_plan_init

static unsigned baseline_builds, candidate_builds;

static void compare_plans(u32 expected)
{
    rsx_vertex_layout_plan baseline, candidate;
    builds = 0;
    baseline_layout(&baseline);
    baseline_builds += builds;
    builds = 0;
    eng_vertex_layout(&candidate);
    candidate_builds += builds;
    assert(baseline.mask == expected && candidate.mask == baseline.mask);
    assert(candidate.stride == baseline.stride && candidate.count == baseline.count);
    assert(memcmp(candidate.attrs, baseline.attrs, sizeof baseline.attrs) == 0);
    assert(memcmp(candidate.slot_by_attr, baseline.slot_by_attr, sizeof baseline.slot_by_attr) == 0);
}

int main(void)
{
    const u32 masks[] = {1, 3, 7, 19, 23, 31};
    for (unsigned draw = 0; draw < 3072; draw++) {
        g.vp_cache_gen++;
        shader_mask = masks[draw % (sizeof masks / sizeof masks[0])];
        compare_plans(shader_mask);
    }
    assert(baseline_builds == 3072 && candidate_builds == 6);
    printf("Repeated layouts over 3072 draws: %u baseline builds, %u candidate builds.\n",
           baseline_builds, candidate_builds);

    for (u32 mask = 1; mask <= 0xFFFFu; mask++) {
        g.vp_cache_gen++;
        shader_mask = mask;
        compare_plans(mask);
        compare_plans(mask);
    }
    available = 0;
    compare_plans(0xFFFFu);
    available = 1;
    exact = 0;
    g.vp_cache_start++;
    shader_mask = 3;
    compare_plans(0xFFFFu);
    exact = 1;
    shader_mask = 0;
    g.vp_cache_gen++;
    compare_plans(0xFFFFu);
    shader_mask = 1;
    result = -1;
    g.vp_cache_gen++;
    compare_plans(0xFFFFu);
    puts("All 65535 nonempty masks, cache slot collisions and shader-analysis fallbacks agreed with the baseline.");
    return 0;
}

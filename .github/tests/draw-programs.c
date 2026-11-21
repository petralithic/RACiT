#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "rsx_draw_engine.h"
#include "rsx_vertex_compact.h"
#include "rsx_vp_decompiler.h"
#include "layout-constructor.inc"

#define ENG_FP_MAX_BYTES 4096u
static struct {
    rsx_dispatch rsx;
    u32 vp_cache_gen, vp_cache_start, vp_cache_instrs;
    u64 vp_cache_hash;
} g;
static unsigned pair_lookups, fp_scans;
static u8 fp[64] = {16, 5};
static u32 vp_inputs = 3, epoch = 1, modified_epoch;
static int watching, mapping_size = sizeof fp;

u32 rsx_dsp_vp_start(const rsx_dispatch* rsx)
{
    assert(rsx == &g.rsx);
    pair_lookups++;
    return 0;
}
u32 rsx_vp_program_size_instrs(const u8* code, u32 limit)
{
    assert(code == (const u8*)g.rsx.vp && limit >= 16);
    return 1;
}
int rsx_vp_analyze_inputs(const u8* code, u32 bytes, rsx_vp_input_analysis* analysis)
{
    assert(code == (const u8*)g.rsx.vp && bytes == 16);
    *analysis = (rsx_vp_input_analysis){vp_inputs, 1};
    return 1;
}
u32 rsx_dsp_fragment_program(const rsx_dispatch* rsx, u32* location)
{
    assert(rsx == &g.rsx);
    *location = 1;
    return 32;
}
static const u8* eng_guest_ptr(void* user, u32 location, u32 offset, u32 bytes)
{
    assert(!user && location == 1 && offset == 32);
    return bytes <= (u32)mapping_size ? fp : NULL;
}
static u32 rsx_fp_program_size(const u8* code, u32 bytes)
{
    assert(code == fp && bytes == ENG_FP_MAX_BYTES);
    fp_scans++;
    return code[0];
}
static u32 rsx_fp_texture_mask(const u8* code, u32 bytes)
{
    assert(code == fp && bytes == code[0]);
    return code[1];
}
static int rsx_ww_on(void) { return watching; }
static u32 rsx_ww_epoch(void) { return epoch; }
static int rsx_ww_written_since(const void* ptr, u32 bytes, u32 since)
{
    assert(ptr == fp && bytes);
    return modified_epoch > since;
}

#include "program-lookup.inc"
#include "texture-mask.inc"

static struct { const u8* vp, *fp; u32 vi, fs; int fixed; } pipeline_input;
static u32 record_pipeline(const u8* vp, const u8* input_fp, u32 vi, u32 fs, int fixed)
{
    pipeline_input.vp = vp;
    pipeline_input.fp = input_fp;
    pipeline_input.vi = vi;
    pipeline_input.fs = fs;
    pipeline_input.fixed = fixed;
    return 1;
}

static u64 eng_rdtsc(void) { return 0; }
#define PSTAGE(i) ((void)_pt)
#include "pipeline-input.inc"

static void check_draw(u32 vertex_mask, u32 texture_mask, u32 fp_size, unsigned scans)
{
    pair_lookups = fp_scans = 0;
    rsx_vertex_layout_plan layout;
    int fixed;
#ifdef BASELINE
    eng_vertex_layout(&layout);
    const u32 actual_mask = eng_fp_tex_mask();
    eng_pipeline_get(&layout, NULL, 0, 0, &fixed);
    assert(pair_lookups == 3);
#else
    const eng_draw_programs programs = eng_guest_programs();
    eng_vertex_layout(&layout, &programs);
    const u32 actual_mask = eng_fp_tex_mask(&programs);
    eng_pipeline_get(&layout, &programs, NULL, 0, 0, &fixed);
    assert(pair_lookups == 1);
#endif
    assert(layout.mask == vertex_mask && actual_mask == texture_mask);
    assert(pipeline_input.fixed == !fp_size && pipeline_input.fs == fp_size);
    assert(pipeline_input.vp == (const u8*)g.rsx.vp && pipeline_input.vi == 1);
    assert(pipeline_input.fp == (fp_size ? fp : NULL));
    assert(fp_scans == scans);
}

int main(void)
{
    g.rsx.regs[0x08E4 >> 2] = 33;
#ifdef BASELINE
    const unsigned no_watch_scans = 3;
#else
    const unsigned no_watch_scans = 1;
#endif
    check_draw(3, 5, 16, no_watch_scans);
    printf("No write tracking: %u pair lookups and %u fragment sizing scans per draw.\n", pair_lookups, fp_scans);
    fp[0] = 32;
    fp[1] = 8;
    vp_inputs = 9;
    g.rsx.vp_gen++;
    check_draw(9, 8, 32, no_watch_scans);

    watching = 1;
    check_draw(9, 8, 32, 1);
    check_draw(9, 8, 32, 0);
    fp[0] = 48;
    fp[1] = 16;
    modified_epoch = ++epoch;
    check_draw(9, 16, 48, 1);
    check_draw(9, 16, 48, 0);

    g.rsx.regs[0x08E4 >> 2] = 0;
    check_draw(0xFFFFu, 0xFFFFu, 0, 0);
    g.rsx.regs[0x08E4 >> 2] = 65;
    mapping_size = 16;
    check_draw(0xFFFFu, 0xFFFFu, 0, no_watch_scans);
    puts("Shader uploads and writes between draws are observed, and incomplete programs use the full fallback masks.");
    return 0;
}

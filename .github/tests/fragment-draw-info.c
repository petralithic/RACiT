#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "rsx_fp_decompiler.h"

extern unsigned test_word_reads;
static u8 program[(RSX_FP_MAX_INLINE_CONSTANTS + 1u) * 32u];
static rsx_fp_constant_block constants;
static u32 last_texture_mask;

static void put_word(u8* p, u32 value)
{
    const u32 swapped = (value << 16) | (value >> 16);
    p[0] = (u8)(swapped >> 24);
    p[1] = (u8)(swapped >> 16);
    p[2] = (u8)(swapped >> 8);
    p[3] = (u8)swapped;
}

static u32 build_program(u32 n, u32 constant_interval)
{
    static const u32 texture_opcodes[] = {0x17u, 0x18u, 0x19u, 0x31u, 0x2Fu};
    u32 offset = 0;
    for (u32 i = 0; i < n; i++) {
        const int constant = constant_interval && i % constant_interval == 0;
        u32 instruction = 0x01001E00u;
        if (i < 5) instruction = (texture_opcodes[i] << 24) | (i << 17);
        if (i == 5) instruction = (0x17u << 24) | (13u << 17);
        put_word(program + offset, instruction | (i + 1u == n));
        put_word(program + offset + 4, constant ? 2u : 1u);
        put_word(program + offset + 8, i == 5 ? (1u << 31) : 0);
        put_word(program + offset + 12, 0);
        offset += 16;
        if (constant) {
            put_word(program + offset, 0x3F800000u + i);
            put_word(program + offset + 4, 0x7FC00000u);
            put_word(program + offset + 8, 0x7F800000u);
            put_word(program + offset + 12, 0xBF800000u);
            offset += 16;
        }
    }
    return offset;
}

static u64 examine(const char* name, const u8* bytes, u32 length, u64 seed, int valid)
{
    constants.count = 0;
    g_rsx_fp_nonfinite_constants = 0;
    test_word_reads = 0;
#ifdef BASELINE
    const u64 hash = rsx_fp_collect_constants_and_hash(bytes, length, seed, &constants);
    last_texture_mask = hash ? rsx_fp_texture_mask(bytes, length) : 0xFFFFu;
#else
    const u64 hash = rsx_fp_analyze_draw(bytes, length, seed, &constants, &last_texture_mask);
#endif
    assert((hash != 0) == valid);
    printf("%s %016llx %u %llu %04x\n", name, (unsigned long long)hash, constants.count,
           g_rsx_fp_nonfinite_constants, last_texture_mask);
    for (u32 slot = 0; slot < constants.count; slot++) {
        for (u32 component = 0; component < 4; component++)
            printf("%08x\n", constants.values[slot][component]);
    }
    return hash;
}

#ifndef BASELINE
typedef struct { const u8* fp; u32 fp_size; int guest; } eng_draw_programs;
static struct { int valid; u32 serial; } s_fpc;
static struct { rsx_fp_constant_block fp_constants; } g;
#include "draw-info.inc"

static void check_draw_cache(void)
{
    const eng_draw_programs guest = {program, build_program(12, 4), 1};
    const eng_draw_programs fixed = {0};
    s_fpc.valid = 1;
    s_fpc.serial = 1;
    test_word_reads = 0;
    eng_fragment_info info = eng_fragment_draw_info(&guest);
    assert(info.hash && info.texture_mask == 0x1Fu);
    assert(test_word_reads && g.fp_constants.count == 3);

    test_word_reads = 0;
    info = eng_fragment_draw_info(&guest);
    assert(info.hash && info.texture_mask == 0x1Fu && test_word_reads == 0);
    info = eng_fragment_draw_info(&fixed);
    assert(!info.hash && info.texture_mask == 0xFFFFu && g.fp_constants.count == 0);
    info = eng_fragment_draw_info(&guest);
    assert(info.hash && info.texture_mask == 0x1Fu && test_word_reads && g.fp_constants.count == 3);

    put_word(program + 0, 0x01001E00u);
    s_fpc.serial++;
    info = eng_fragment_draw_info(&guest);
    assert(info.hash && info.texture_mask == 0x1Eu);
    put_word(program + 32, 0x01001E00u);
    s_fpc.valid = 0;
    info = eng_fragment_draw_info(&guest);
    assert(info.hash && info.texture_mask == 0x1Cu);
    fputs("The draw cache handles tracked changes, untracked draws and fixed shader fallback.\n", stderr);
}
#endif

int main(void)
{
    const u64 seed = 1469598103934665603ull;
    u32 length = build_program(64, 4);
    const u64 hash = examine("mixed instructions", program, length, seed, 1);
    assert(last_texture_mask == 0x1Fu);
#ifdef BASELINE
    assert(test_word_reads == 576);
#else
    assert(test_word_reads == 320);
#endif
    fprintf(stderr, "64 instructions and 16 inline slots: %u decoded word reads.\n", test_word_reads);
    put_word(program + 16, 0x40000000u);
    assert(examine("changed inline payload", program, length, seed, 1) == hash);
    put_word(program + 0, 0x01001E02u);
    assert(examine("changed instruction", program, length, seed, 1) != hash);
    examine("missing terminator", program, length - 16, seed, 0);
    examine("truncated inline slot", program, 16, seed, 0);
    examine("null program", NULL, 0, seed, 0);

    length = build_program(12, 0);
    examine("no inline slots", program, length, 0xDEADBEEFu, 1);
    length = build_program(RSX_FP_MAX_INLINE_CONSTANTS, 1);
    examine("full inline block", program, length, seed, 1);
    length = build_program(RSX_FP_MAX_INLINE_CONSTANTS + 1u, 1);
    examine("too many inline slots", program, length, seed, 0);
#ifndef BASELINE
    check_draw_cache();
#endif
    return 0;
}

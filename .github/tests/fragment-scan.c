#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "rsx_fp_decompiler.h"

extern unsigned test_word_reads;
static u8 program[(RSX_FP_MAX_INLINE_CONSTANTS + 1u) * 32u];
static rsx_fp_constant_block constants;

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
    u32 offset = 0;
    for (u32 i = 0; i < n; i++) {
        const int constant = constant_interval && i % constant_interval == 0;
        put_word(program + offset, 0x01001E00u | (i + 1u == n));
        put_word(program + offset + 4, constant ? 2u : 1u);
        put_word(program + offset + 8, 0);
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
    const int count = rsx_fp_collect_constants(bytes, length, &constants);
    const u64 hash = count < 0 ? 0 : rsx_fp_structural_hash(bytes, length, seed);
#else
    const u64 hash = rsx_fp_collect_constants_and_hash(bytes, length, seed, &constants);
#endif
    assert((hash != 0) == valid);
    printf("%s %016llx %u %llu\n", name, (unsigned long long)hash, constants.count,
           g_rsx_fp_nonfinite_constants);
    for (u32 slot = 0; slot < constants.count; slot++) {
        for (u32 component = 0; component < 4; component++)
            printf("%08x\n", constants.values[slot][component]);
    }
    return hash;
}

int main(void)
{
    const u64 seed = 1469598103934665603ull;
    u32 length = build_program(64, 4);
    const u64 hash = examine("mixed instructions", program, length, seed, 1);
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
    return 0;
}

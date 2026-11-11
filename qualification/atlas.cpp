#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
static std::vector<uint8_t> memory(0x40000);
static uint8_t* vm_base = memory.data();
static uint32_t s_font[16] = {0x1060};
static int s_nfont = 1;
static uint16_t be16(const uint8_t* p) { return (uint16_t)(p[0] << 8 | p[1]); }
static bool readable(uint32_t ea, uint32_t n) { return (uint64_t)ea + n <= memory.size(); }
static const uint8_t* font_page_at(uint32_t, int, uint32_t* pitch,int* width,int* height) { *width=512;*height=256;*pitch=2048;return vm_base+0x4000; }
#include "dxt_helpers.h"
#include "desktop_prompts.h"
static void put16(uint8_t* p, unsigned value) { p[0]=(uint8_t)(value>>8);p[1]=(uint8_t)value; }
int main()
{
    for (unsigned key = 0; key < 14; ++key) {
        uint8_t* rec = vm_base + 0x1000 + key * 16;
        put16(rec,0xE001+key);put16(rec+2,key%4*128+4);put16(rec+4,key/4*64+4);
        rec[10]=key<6?52:87;rec[11]=52;
    }
    const auto original = memory;
    desktop_prompts(true); assert(desktop_count == 14);
    const auto keyboard = memory; assert(keyboard != original);
    desktop_prompts(true); assert(memory == keyboard);
    desktop_prompts(false); assert(memory == original);
    desktop_prompts(true); assert(memory == keyboard);
    puts("Actual keycap helper and DXT5 codec pass 14 synthetic glyphs, repeated application and controller restoration.");
}

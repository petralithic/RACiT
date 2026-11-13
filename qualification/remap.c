#include <stdio.h>
#include <string.h>
#include "rsx_dispatch.h"
#include "rsx_texture_layout.h"
static struct { rsx_dispatch rsx; } g;
static rsx_dsp_texture inputs[RSX_DSP_NUM_TEXTURES];
void rsx_dsp_get_texture(const rsx_dispatch* state,u32 unit,rsx_dsp_texture* out)
{
    (void)state;*out=inputs[unit];
}
/* ACTUAL_SIGNED_MASK */
int main(void)
{
    const u32 remaps[]={0,0xAAE4u,0x40000000u,0xFFE4u,0x00E4u,0x55E4u};
    const u32 units[]={0,15};
    unsigned cases=0,failed=0;
    u8 identity[4];rsx_texture_component_remap(0xAAE4u,0x85u,identity);
    for(u32 sign=0;sign<16;++sign) {
        u8 rgba[4];
        for(u32 channel=0;channel<4;++channel)
            rgba[identity[channel]]=(sign&(1u<<channel))?0x11u:0x22u;
        for(unsigned r=0;r<sizeof remaps/sizeof remaps[0];++r) {
            u8 selectors[4];rsx_texture_component_remap(remaps[r],0x85u,selectors);
            for(unsigned u=0;u<sizeof units/sizeof units[0];++u) {
                const u32 unit=units[u];memset(inputs,0,sizeof inputs);
                inputs[unit].enabled=1;inputs[unit].format=0x85u;
                inputs[unit].filter=sign<<28;inputs[unit].remap=remaps[r];
                u64 expected=0;
                for(unsigned channel=0;channel<4;++channel) {
                    const u8 selector=selectors[channel];
                    const u8 value=selector==RSX_REMAP_ZERO?0u:selector==RSX_REMAP_ONE?255u:rgba[selector];
                    if(value==0x11u) expected|=(u64)1<<(unit*4u+identity[channel]);
                }
                const u64 actual=eng_signed_mask();++cases;
                if(actual!=expected) {
                    printf("mismatch unit=%u signed=%X remap=%08X expected=%016llX actual=%016llX\n",unit,sign,remaps[r],(unsigned long long)expected,(unsigned long long)actual);
                    ++failed;
                }
            }
        }
    }
    printf("cases=%u passed=%u failed=%u\n",cases,cases-failed,failed);
    return failed?1:0;
}

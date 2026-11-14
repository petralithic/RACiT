#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "rsx_host_ui.h"
typedef struct { unsigned rectangles, labels; } ui_out;
static double s_ft[128];
static unsigned s_ft_n;
static char s_fps_text[64];
#define N_OF(a) (sizeof(a)/sizeof((a)[0]))
#define LOCK() ((void)0)
#define UNLOCK() ((void)0)
static uint32_t rgba(int r,int g,int b,int a) { return (uint32_t)r | (uint32_t)g<<8 | (uint32_t)b<<16 | (uint32_t)a<<24; }
static void rect(ui_out* out,float x,float y,float right,float bottom,uint32_t color)
{
    (void)color;
    assert(isfinite(x) && isfinite(y) && isfinite(right) && isfinite(bottom));
    assert(right >= x && bottom >= y);
    ++out->rectangles;
}
static void text(ui_out* out,float x,float y,const char* label,uint32_t color)
{
    (void)color;assert(isfinite(x) && isfinite(y) && label[0]);++out->labels;
}
#include "rsx_perf_graph.h"
int main(void)
{
    rsx_ui_font font={0};font.px=16;font.line_h=20;
    const unsigned counts[]={0,1,128,129,384};
    for(unsigned c=0;c<N_OF(counts);++c) {
        s_ft_n=counts[c];
        for(unsigned i=0;i<N_OF(s_ft);++i) s_ft[i]=i%3?16.666667:0;
        ui_out out={0};racit_perf_overlay(&out,&font);
        const unsigned visible=s_ft_n<128?s_ft_n:128;
        assert(out.rectangles==5+2*visible && out.labels==3);
    }
    puts("Actual graph helper passed empty, single, full and wrapped ring geometry.");
}

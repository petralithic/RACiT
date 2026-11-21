#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint32_t u32;
typedef u32 VkFramebuffer, VkRenderPass, VkImageView;
enum { VK_NULL_HANDLE = 0, VK_SUCCESS = 0, VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO = 1 };
#define RSX_BE_MAX_COLOR_TARGETS 4
#include "framebuffer_limit.inc"
typedef struct {
    u32 sType;
    VkRenderPass renderPass;
    u32 attachmentCount;
    const VkImageView* pAttachments;
    u32 width, height, layers;
} VkFramebufferCreateInfo;

static struct {
    struct { u32 key[RSX_BE_MAX_COLOR_TARGETS + 1]; VkFramebuffer fb; } fb[VK_ENG_FB_CACHE];
    u32 fb_count, fb_recent;
} s_e2;
static struct { u32 device; } s_vk;
static struct { struct { VkImageView view; } im; } s_eobj[128];
static struct { int recording; VkFramebuffer fb; } s_bt;
static u32 s_vkc[3];
static unsigned comparisons, ended_passes, creations;
static int reject_create, defer_destruction;
static int live[512];   /* 1 owned, 2 deferred */

static void vk_bt_end_pass(void) { ended_passes++; }
static int vk_bt_defer_framebuffer(VkFramebuffer fb)
{
    assert(live[fb] == 1);
    if (defer_destruction) live[fb] = 2;
    return defer_destruction;
}
static void pvkDestroyFramebuffer(u32 dev, VkFramebuffer fb, const void* alloc)
{
    assert(dev == s_vk.device && !alloc && live[fb] == 1);
    live[fb] = 0;
}
static int pvkCreateFramebuffer(u32 dev, const VkFramebufferCreateInfo* ci, const void* alloc,
                                VkFramebuffer* fb)
{
    assert(dev == s_vk.device && !alloc && ci->sType == VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO);
    assert(ci->renderPass == 1 && ci->attachmentCount == 2 && ci->layers == 1);
    assert(ci->width == 160 && ci->height == 90 && ci->pAttachments[1] == 1000);
    if (reject_create) return -1;
    *fb = ++creations;
    assert(*fb < sizeof live / sizeof live[0]);
    live[*fb] = 1;
    return VK_SUCCESS;
}
static int compare_key(const void* a, const void* b, size_t bytes)
{
    comparisons++;
    return memcmp(a, b, bytes);
}
#define memcmp compare_key
#include "framebuffer_source.inc"
#undef memcmp

static VkFramebuffer lookup(u32 target)
{
    return vk_eng_framebuffer(1, &target, 1, 1000, 96, 160, 90);
}

int main(void)
{
    for (u32 handle = 1; handle < 96; handle++) s_eobj[handle].im.view = 200 + handle;
    VkFramebuffer cached[17];
    for (u32 handle = 1; handle <= 16; handle++) cached[handle] = lookup(handle);
    comparisons = 0;
    for (int i = 0; i < 100; i++) assert(lookup(16) == cached[16]);
    assert(creations == 16);
#ifdef BASELINE
    assert(comparisons == 1600);
    printf("Baseline comparisons for 100 draws to one cached framebuffer: %u\n", comparisons);
#else
    assert(comparisons == 100);
    printf("Candidate comparisons for 100 draws to one cached framebuffer: %u\n", comparisons);
#endif
    assert(lookup(9) == cached[9]);
    assert(lookup(9) == cached[9]);
    vk_eng_drop_framebuffers_using(1);
    assert(live[cached[1]] == 0 && lookup(9) == cached[9]);
    s_bt.recording = 1;
    s_bt.fb = cached[9];
    defer_destruction = 1;
    vk_eng_drop_framebuffers_using(9);
    assert(ended_passes == 1 && s_bt.fb == VK_NULL_HANDLE && live[cached[9]] == 2);
    s_bt.recording = 0;
    defer_destruction = 0;
    reject_create = 1;
    assert(lookup(9) == VK_NULL_HANDLE && lookup(16) == cached[16]);
    reject_create = 0;
    assert(lookup(9) != cached[9] && creations == 17);
    vk_eng_drop_framebuffers();
    assert(s_e2.fb_count == 0 && live[cached[16]] == 0);
    for (u32 handle = 1; handle <= VK_ENG_FB_CACHE; handle++) assert(lookup(handle));
    VkFramebuffer oldest = lookup(1);
    assert(s_e2.fb_count == VK_ENG_FB_CACHE);
    assert(lookup(VK_ENG_FB_CACHE + 1));
    assert(s_e2.fb_count == 1 && live[oldest] == 0 && lookup(1) != oldest);
    puts("Attachment release, compaction, deferred destruction, failed creation, and capacity reset passed.");
}

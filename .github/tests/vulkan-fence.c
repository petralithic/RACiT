#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <vulkan/vulkan.h>

typedef uint32_t u32;
#define VK_BT_SLOTS 3
typedef struct {
    VkFence fence;
    VkCommandBuffer cmd;
    VkDescriptorPool pool;
    unsigned long long used;
    int submitted;
} vk_bt_slot;

static struct { VkDevice device; VkQueue queue; VkCommandBuffer cmd; } s_vk;
static struct { vk_bt_slot s[VK_BT_SLOTS]; u32 cur, epoch; int recording; } s_bt;
unsigned long long g_rsx_gpu_wait_us;
unsigned long long ps3_qpc_us(void) { return 0; }

static u32 waits, wait_count, resets, submits, retired, faults;
static VkResult wait_result, fence_reset_result, pool_reset_result;
static int timeout_once;
static void vk_device_lost_report(VkResult result) { if (result == VK_ERROR_DEVICE_LOST) faults++; }
#define VK_LOG(...) fprintf(stderr, __VA_ARGS__)
#define VK_CHECK(call, what) do { VkResult result = (call); if (result != VK_SUCCESS) { vk_device_lost_report(result); return -1; } } while (0)
static void vk_bt_free_garbage(vk_bt_slot* slot) { assert(!slot->submitted); retired++; }
static void vk_bt_end_pass(void) {}

static VkResult pvkWaitForFences(VkDevice device, u32 count, const VkFence* fences,
                                 VkBool32 all, uint64_t timeout)
{
    assert(count && all && timeout == 2000000000ull);
    for (u32 i = 0; i < count; i++) {
        uintptr_t slot = (uintptr_t)fences[i];
        assert(slot >= 1 && slot <= VK_BT_SLOTS && s_bt.s[slot - 1].submitted);
    }
    waits++;
    wait_count = count;
    if (timeout_once) { timeout_once = 0; return VK_TIMEOUT; }
    return wait_result;
}

static VkResult pvkResetFences(VkDevice d, u32 count, const VkFence* fences)
{ assert(count == 1 && fences[0]); resets++; return fence_reset_result; }
static VkResult pvkResetDescriptorPool(VkDevice d, VkDescriptorPool p, VkDescriptorPoolResetFlags f)
{ return pool_reset_result; }
static VkResult pvkResetCommandBuffer(VkCommandBuffer cmd, VkCommandBufferResetFlags f)
{ return VK_SUCCESS; }
static VkResult pvkBeginCommandBuffer(VkCommandBuffer cmd, const VkCommandBufferBeginInfo* info)
{ return VK_SUCCESS; }
static VkResult pvkEndCommandBuffer(VkCommandBuffer cmd) { return VK_SUCCESS; }
static VkResult pvkQueueSubmit(VkQueue q, u32 n, const VkSubmitInfo* info, VkFence fence)
{ assert(n == 1 && info->commandBufferCount == 1 && fence); submits++; return VK_SUCCESS; }

#include "wait-section.inc"

static void set_up(u32 pending)
{
    memset(&s_bt, 0, sizeof(s_bt));
    waits = wait_count = resets = submits = retired = faults = 0;
    timeout_once = 0;
    wait_result = fence_reset_result = pool_reset_result = VK_SUCCESS;
    for (u32 i = 0; i < VK_BT_SLOTS; i++) {
        s_bt.s[i].fence = (VkFence)(uintptr_t)(i + 1);
        s_bt.s[i].submitted = i < pending;
    }
}

int main(void)
{
    set_up(3);
    assert(vk_bt_submit(1, VK_NULL_HANDLE, VK_NULL_HANDLE) == 0);
    assert(retired == 3);
#ifdef BASELINE
    assert(waits == 3 && wait_count == 1 && resets == 3);
#else
    assert(waits == 1 && wait_count == 3 && resets == 0);
#endif
    printf("Three pending batches retired after %u wait calls.\n", waits);

    set_up(3);
    wait_result = VK_ERROR_DEVICE_LOST;
    int status = vk_bt_submit(1, VK_NULL_HANDLE, VK_NULL_HANDLE);
    assert(retired == 0 && resets == 0 && faults && s_bt.s[0].submitted && s_bt.s[1].submitted && s_bt.s[2].submitted);
#ifdef BASELINE
    assert(status == 0 && waits == 3);
    puts("Baseline reports success on a failed driver wait.");
#else
    assert(status < 0 && waits == 1);
    puts("Failed driver wait reaches the caller with every resource still pending.");

    set_up(2);
    s_bt.cur = 2;
    assert(vk_bt_open() == 0);
    timeout_once = 1;
    assert(vk_bt_submit(1, VK_NULL_HANDLE, VK_NULL_HANDLE) == 0);
    assert(waits == 2 && wait_count == 3 && retired == 3 && resets == 1 && submits == 1);

    set_up(2);
    s_bt.cur = 2;
    assert(vk_bt_open() == 0 && vk_bt_submit(0, VK_NULL_HANDLE, VK_NULL_HANDLE) == 0);
    assert(submits == 1 && !waits && !retired);
    assert(vk_bt_open() == 0 && waits == 1 && retired == 1);
    assert(!s_bt.s[0].submitted && s_bt.s[1].submitted && s_bt.s[2].submitted);

    set_up(1);
    s_bt.cur = 1;
    assert(vk_bt_open() == 0);
    fence_reset_result = VK_ERROR_OUT_OF_DEVICE_MEMORY;
    assert(vk_bt_submit(1, VK_NULL_HANDLE, VK_NULL_HANDLE) < 0);
    assert(!submits && !waits && s_bt.s[0].submitted && !s_bt.s[1].submitted);

    set_up(1);
    s_bt.s[0].pool = (VkDescriptorPool)(uintptr_t)1;
    pool_reset_result = VK_ERROR_UNKNOWN;
    assert(vk_bt_open() < 0);
    assert(retired == 1 && wait_count == 1 && !s_bt.recording);

    set_up(0);
    assert(vk_bt_submit(1, VK_NULL_HANDLE, VK_NULL_HANDLE) == 0 && !waits);
    puts("Timeout retry, asynchronous slot reuse, reset failures and empty drain checked.");
#endif
    return 0;
}

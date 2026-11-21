#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

using u32 = uint32_t;
using u64 = uint64_t;
#include "prep_group_types.inc"

using VkShaderModule = u64;
using VkRenderPass = u32;
using VkPipeline = u32;
constexpr int VK_NULL_HANDLE = 0;
constexpr int VK_SUCCESS = 0;
constexpr int VK_SPV_WORDS = 16;
constexpr int RSX_BE_MAX_COLOR_TARGETS = 4;
#define SDLCALL
using SDL_atomic_t = std::atomic<int>;
static int SDL_AtomicAdd(SDL_atomic_t* value, int delta) { return value->fetch_add(delta); }
static void SDL_AtomicIncRef(SDL_atomic_t* value) { ++*value; }
static struct {
    SDL_atomic_t next, built, failed, absent;
    const vk_pw_spv* sp;
    u32 ns;
    const char* spv_path;
} s_prep;
static struct { vk_vdesc* recs; u32 n; } s_pw;
static struct { int device; } s_vk;
static struct { int pcache; } s_g;
constexpr u32 variants = 37;
static std::array<std::atomic<int>, variants> visited;
static std::atomic<int> modules, destroyed, invalid;

static VkShaderModule shader_id(u64 hash, u32 words) { return hash * 16 + words; }
static VkShaderModule vk_pw_module(FILE*, const vk_pw_spv* sp, u32*)
{
    ++modules;
    return shader_id(sp->hash, sp->words);
}
static void pvkDestroyShaderModule(int, VkShaderModule, void*) { ++destroyed; }
static VkRenderPass vk_eng_make_render_pass(u32, u32, int) { return 1; }
static void pvkDestroyRenderPass(int, VkRenderPass, void*) {}
static void pvkDestroyPipeline(int, VkPipeline, void*) {}
static int vk_vdesc_build(const vk_vdesc* d, VkShaderModule vm, VkShaderModule fm,
                          VkRenderPass, VkPipeline* pipeline, int, int)
{
    if (vm != shader_id(d->vs_hash, d->vs_words) ||
        fm != shader_id(d->fs_hash, d->fs_words)) ++invalid;
    ++visited.at(d->topology);
    *pipeline = 1;
    return VK_SUCCESS;
}

#include "prep_group_worker.inc"

static int run(bool grouped, bool parallel)
{
    std::array<vk_vdesc, variants> recs{};
    std::array<vk_pw_spv, 8> programs{};
    for (u32 index = 0; index < variants; ++index) {
        const auto shader = index % 4;
        auto& rec = recs[index];
        rec.topology = index;
        rec.vs_hash = 100 + shader % 2;
        rec.vs_words = 4 + shader / 2;
        rec.fs_hash = 200 + shader % 2;
        rec.fs_words = 6 + shader / 2;
        programs[shader] = {rec.vs_hash, rec.vs_words, 0};
        programs[shader + 4] = {rec.fs_hash, rec.fs_words, 0};
        visited[index] = 0;
    }
    modules = destroyed = invalid = 0;
    s_prep.next = s_prep.built = s_prep.failed = s_prep.absent = 0;
    qsort(programs.data(), programs.size(), sizeof programs[0], vk_pw_spv_cmp);
    s_prep.sp = programs.data();
    s_prep.ns = static_cast<u32>(programs.size());
    const u32 n = variants;
    s_pw = {recs.data(), n};
    if (grouped) {
#include "prep_group_sort.inc"
    }
    if (parallel) {
        std::array<std::thread, 4> workers;
        for (auto& worker : workers) worker = std::thread(vk_prep_worker, nullptr);
        for (auto& worker : workers) worker.join();
    } else {
        vk_prep_worker(nullptr);
    }
    if (s_prep.built != static_cast<int>(variants) || s_prep.failed || s_prep.absent ||
        invalid || modules != destroyed) return -1;
    if (std::any_of(visited.begin(), visited.end(), [](const auto& count) { return count != 1; })) return -1;
    return modules;
}

int main()
{
    s_prep.spv_path = "synthetic-shaders.bin";
    FILE* file = std::fopen(s_prep.spv_path, "wb");
    if (!file || std::fclose(file) != 0) return 2;
    const int original = run(false, false);
    const int grouped = run(true, false);
    const int parallel = run(true, true);
    if (original != 74 || grouped != 8 || parallel < 8 || parallel >= original) {
        std::fprintf(stderr, "Unexpected module counts or coverage: %d, %d, %d\n", original, grouped, parallel);
        return 1;
    }
    std::printf("37 variants completed exactly once with the correct shaders. Module calls: %d before, %d grouped, %d across four workers.\n",
                original, grouped, parallel);
}

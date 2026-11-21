#include <cstdint>
#include <cstdio>
#include <cstdlib>

using VkDeviceSize = uint64_t;
using VkBuffer = uint64_t;
constexpr VkBuffer VK_NULL_HANDLE = 0;
constexpr int VK_INDEX_TYPE_UINT32 = 1;
static struct { unsigned epoch = 1; } s_bt;
static struct { int cmd = 1; } s_vk;
static struct { VkBuffer buf = 2; } s_geo_heap;
static struct {
    unsigned reuse_epoch, dyn_epoch;
    VkBuffer bound_vbuf, bound_ibuf;
    VkDeviceSize bound_vo, bound_io;
} s_e2;
static bool s_vk_foreign_bind, s_draw_geom_vi;
static int s_draw_vofs;

struct Binding { VkBuffer buffer; VkDeviceSize offset; };
static Binding driver_vertex, driver_index, expected_vertex, expected_index;
static int vertex_binds, index_binds, draws, expected_base;

static void require(bool pass, const char* message)
{
    if (!pass) {
        std::fprintf(stderr, "%s\n", message);
        std::exit(1);
    }
}

static void pvkCmdBindVertexBuffers(int, unsigned first, unsigned count, const VkBuffer* buf, const VkDeviceSize* off)
{
    require(first == 0 && count == 1, "vertex binding range");
    driver_vertex = {*buf, *off};
    ++vertex_binds;
}
static void pvkCmdBindIndexBuffer(int, VkBuffer buf, VkDeviceSize off, int type)
{
    require(type == VK_INDEX_TYPE_UINT32, "index type");
    driver_index = {buf, off};
    ++index_binds;
}
static void pvkCmdDraw(int, unsigned, unsigned, unsigned, unsigned)
{
    require(driver_vertex.buffer == expected_vertex.buffer && driver_vertex.offset == expected_vertex.offset,
            "draw must use the intended vertex binding");
    ++draws;
}
static void pvkCmdDrawIndexed(int cmd, unsigned, unsigned, unsigned, int base, unsigned)
{
    require(driver_index.buffer == expected_index.buffer && driver_index.offset == expected_index.offset &&
            base == expected_base, "indexed draw must use intended indices and base vertex");
    pvkCmdDraw(cmd, 3, 1, 0, 0);
}

static void draw(Binding vertices, Binding indices, int base = 0)
{
    expected_vertex = vertices;
    expected_index = indices;
    expected_base = base;
    const VkBuffer vbuf = vertices.buffer;
    const VkDeviceSize vo = vertices.offset, io = indices.offset;
    const bool gidx = indices.buffer == s_geo_heap.buf;
    const unsigned index_count = indices.buffer ? 3 : 0, vertex_count = 3;
    const VkDeviceSize ibytes = index_count * 4u;
    struct Slot { Binding arena; } slot{{indices.buffer, 0}};
    auto* sl = &slot;
    s_draw_geom_vi = base != 0;
    s_draw_vofs = base;
#include "geometry_epoch.inc"
#include "geometry_draw.inc"
}

int main()
{
    for (int i = 0; i < 100; ++i) draw({1, 32}, {2, 64});
    require(draws == 100 && vertex_binds == 1 && index_binds == 1, "repeated draws need only one bind per buffer");
    draw({1, 32}, {0, 0});
    draw({1, 32}, {2, 64});
    require(vertex_binds == 1 && index_binds == 1, "plain draw retains same-batch index binding");

    draw({1, 96}, {2, 128});
    draw({4, 96}, {5, 128}, -2);
    require(vertex_binds == 3 && index_binds == 3, "changed offsets and changed handles each rebind");

    ++s_bt.epoch;
    driver_vertex = driver_index = {};
    draw({4, 96}, {0, 0});
    draw({4, 96}, {5, 128}, -2);
    require(vertex_binds == 4 && index_binds == 4, "new batch forgets old bindings even after a plain draw");

    s_vk_foreign_bind = true;
    driver_vertex = {8, 9};
    draw({4, 96}, {5, 128}, -2);
    require(vertex_binds == 5 && index_binds == 5 && draws == 107, "foreign drawing conservatively rebinds both buffers");
    std::puts("100 repeated indexed draws used one binding per buffer. Changed buffers and offsets, a new batch, and a foreign draw all rebound correctly.");
}

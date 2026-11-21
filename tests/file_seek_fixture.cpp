#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#include <io.h>
#endif
#include "runtime/platform/host_fs_seek.h"

using u64 = uint64_t;
using s64 = int64_t;
using s32 = int32_t;
using CellFsFd = s32;

#define CELL_FS_ERROR_EBADF ((s32)CELL_EBADF)
#define CELL_FS_EIO ((s32)CELL_EIO)
#define CELL_FS_S_IFREG 0x8000u
constexpr int FS_MAX = 8;
constexpr int MAX_OPEN_FILES = FS_MAX;
constexpr int SYS_FS_FD_MAX = FS_MAX;
constexpr int fd = 3;
constexpr uint32_t out_address = 32;

struct ppu_context { uint64_t gpr[32]{}; };
struct { bool in_use; FILE* host_fp; } s_files[MAX_OPEN_FILES];
struct sys_fs_fd_info { bool active; FILE* fp; };
static sys_fs_fd_info g_sys_fs_fds[SYS_FS_FD_MAX];
static FILE* g_files[FS_MAX];
static char g_fd_path[FS_MAX][64];
static thread_local int t_seek_fd;
static thread_local long long t_seek_pos;
static uint8_t result_bytes[8];

#define LV2_ARG_S32(ctx, n) ((int32_t)(ctx)->gpr[3 + (n)])
#define LV2_ARG_S64(ctx, n) ((int64_t)(ctx)->gpr[3 + (n)])
#define LV2_ARG_PTR(ctx, n) ((uint32_t)(ctx)->gpr[3 + (n)])

static void require(bool pass, const char* what)
{
    if (!pass) throw std::runtime_error(what);
}

static void* gptr(void* address) { return address; }

static u64 ps3_bswap64(u64 value)
{
    u64 flipped = 0;
    for (int i = 0; i < 8; ++i) flipped = (flipped << 8) | ((value >> (8 * i)) & 0xff);
    return flipped;
}

static void vm_write64(uint32_t address, uint64_t value)
{
    require(address == out_address, "Wrong output address");
    for (int i = 0; i < 8; ++i) result_bytes[i] = (uint8_t)(value >> (8 * (7 - i)));
}

static void write_be64(uint32_t address, uint64_t value) { vm_write64(address, value); }
static void write_stat(uint32_t address, uint32_t, uint64_t size) { vm_write64(address, size); }
static void ppu_guest_caller(char*, size_t) {}

#include "file_seek_source.inc"

enum class Route { Hle, Guest, Raw };

static uint64_t result_value()
{
    uint64_t result = 0;
    for (uint8_t byte : result_bytes) result = (result << 8) | byte;
    return result;
}

static int32_t seek(Route route, int64_t offset, int origin, bool write_result = true)
{
    if (route == Route::Hle) {
        uint64_t hle_out = 0;
        memcpy(&hle_out, result_bytes, sizeof hle_out);
        const int32_t status = cellFsLseek(fd, offset, origin, write_result ? &hle_out : nullptr);
        memcpy(result_bytes, &hle_out, sizeof hle_out);
        return status;
    }
    ppu_context ctx;
    ctx.gpr[3] = fd;
    ctx.gpr[4] = (uint64_t)offset;
    ctx.gpr[5] = origin;
    ctx.gpr[6] = write_result ? out_address : 0;
    if (route == Route::Raw) return (int32_t)sys_fs_lseek(&ctx);
    cellFsLseek(&ctx);
    return (int32_t)ctx.gpr[3];
}

int main()
{
    try {
        FILE* file = fopen("sparse-archive.dat", "w+b");
        require(file != nullptr, "Could not open the synthetic archive");
#ifdef _WIN32
        HANDLE handle = (HANDLE)_get_osfhandle(_fileno(file));
        DWORD returned = 0;
        require(DeviceIoControl(handle, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &returned, nullptr) != 0,
                "The Windows test volume must support sparse files");
#endif
        constexpr int64_t offset = (3LL << 30) + 11;
        require(ps3_host_seek64(file, offset, SEEK_SET) == 0 && fputc(0x5a, file) == 0x5a && fflush(file) == 0,
                "Could not write the marker to the sparse archive");
        s_files[fd] = {true, file};
        g_files[fd] = file;
        g_sys_fs_fds[fd - 3] = {true, file};

        for (Route route : {Route::Hle, Route::Guest, Route::Raw}) {
            require(seek(route, offset, 0) == CELL_OK && result_value() == offset, "Large absolute seek failed");
            require(fgetc(file) == 0x5a, "The read did not reach the high-offset marker");
            require(seek(route, -1, 2) == CELL_OK && result_value() == offset, "Large end-relative seek failed");
            require(seek(route, -1, 1) == CELL_OK && result_value() == offset - 1, "Large relative seek failed");

            constexpr uint64_t sentinel = 0x123456789abcdef0ULL;
            vm_write64(out_address, sentinel);
            require(seek(route, 0, 7) == (int32_t)CELL_EINVAL && result_value() == sentinel
                    && ps3_host_tell64(file) == offset - 1, "An invalid origin changed the file or output position");
            require(seek(route, -1, 0) != CELL_OK && result_value() == sentinel,
                    "A host seek failure was reported as success or overwrote the position");
            require(seek(route, offset, 0, false) == CELL_OK && ps3_host_tell64(file) == offset,
                    "A seek without an output position failed");
        }

        ppu_context ctx;
        ctx.gpr[3] = fd;
        ctx.gpr[4] = out_address;
        cellFsFstat(&ctx);
        require(ctx.gpr[3] == CELL_OK && result_value() == offset + 1 && ps3_host_tell64(file) == offset,
                "PPU fstat changed the cursor or reported the wrong large-file size");
        require(fclose(file) == 0 && remove("sparse-archive.dat") == 0, "Could not clean the synthetic archive");
        std::puts("All three seek paths read the marker beyond 3 GiB and preserve output across failed seeks.");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}

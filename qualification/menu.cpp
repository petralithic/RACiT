#include <windows.h>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include "ppu_context.h"
#include "../memory/vm.h"

extern "C" void vm_write32(uint64_t address, uint32_t value);
#include "menu_keycaps.h"
struct ReadOnlyHook {
    bool (*accepts)(unsigned);
    unsigned read;
    unsigned writes[4];
};
#include "readonly.h"

uint8_t* vm_base;
PPU_THREAD_LOCAL void (*g_trampoline_fn)(void*);
static unsigned calls, continuations;
static std::string last_name, last_text;
extern bool racit_menu_archive_init();
extern "C" void racit_menu_archive_path(char*, size_t, int);
extern "C" int racit_keyboard_prompts_active() { return 1; }
extern "C" void vm_write32(uint64_t address, uint32_t value)
{
    const uint32_t big = __builtin_bswap32(value);
    memcpy(vm_base + address, &big, sizeof big);
}
static void finish_guest(void* opaque)
{
    ++continuations;
    static_cast<ppu_context*>(opaque)->gpr[3] = 1;
}
static void old_continuation(void*) {}
extern "C" void ps3_indirect_call(ppu_context* context)
{
    assert(context->ctr == 0x1234 && context->gpr[2] == 0x5678);
    assert(context->gpr[3] == 0x2000 && context->gpr[6] == 1);
    last_name = reinterpret_cast<char*>(vm_base + context->gpr[4]);
    uint32_t string;
    memcpy(&string, vm_base + context->gpr[5] + 8, 4);
    last_text = reinterpret_cast<char*>(vm_base + __builtin_bswap32(string));
    ++calls;
    context->gpr[12] = 0xbad;
    g_trampoline_fn = finish_guest;
}
int main()
{
    assert(_putenv_s("RACACIT_MENU_ARCHIVE", "") == 0);
    assert(racit_menu_archive_init());
    const auto archive = std::filesystem::absolute("qualification-menu.synthetic.psarc");
    unsigned char header[62] = {};
    memcpy(header, "PSAR", 4); memcpy(header + 8, "zlib", 4);
    header[15] = 62; header[19] = 30; header[23] = 1; header[25] = 1;
    { std::ofstream file(archive, std::ios::binary); file.write(reinterpret_cast<char*>(header), sizeof header); }
    assert(_putenv_s("RACACIT_MENU_ARCHIVE", archive.string().c_str()) == 0);
    assert(racit_menu_archive_init());
    for (const ReadOnlyHook& hook : readonly_hooks) {
        char path[2048] = "Z:\\PACKED\\GAME\\GLOBAL_CACHED.PSARC";
        assert(hook.accepts(hook.read));
        racit_menu_archive_path(path, sizeof path, hook.accepts(hook.read));
        assert(path == std::filesystem::canonical(archive).string());
        for (unsigned flags : hook.writes) {
            strcpy(path, "Z:\\packed\\game\\global_cached.psarc");
            assert(!hook.accepts(flags));
            racit_menu_archive_path(path, sizeof path, hook.accepts(flags));
            assert(!strcmp(path, "Z:\\packed\\game\\global_cached.psarc"));
        }
    }
    vm_base = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    assert(vm_base);
    vm_write32(0x1004, 0x2000); vm_write32(0x2000, 0x3000);
    vm_write32(0x3030, 0x4000); vm_write32(0x4000, 0x1234); vm_write32(0x4004, 0x5678);
    ppu_context context{}; context.gpr[1] = 0x8000; context.gpr[3] = 0x1000;
    const ppu_context saved = context;
    g_trampoline_fn = old_continuation;
    racit_publish_menu_keyboard(&context);
    assert(calls == 15 && continuations == 15);
    assert(last_name == "_root.__rccit_mode" && last_text == "keyboard");
    assert(!memcmp(&context, &saved, sizeof context) && g_trampoline_fn == old_continuation);
    DWORD previous;
    assert(VirtualProtect(vm_base + 0xf000, 0x1000, PAGE_NOACCESS, &previous));
    context.gpr[3] = 0xeffc;
    racit_publish_menu_keyboard(&context);
    assert(calls == 15);
    char label[16];
    assert(_putenv_s("RACACIT_BIND_L2", "Mouse3,Return") == 0);
    racit_menu_key_label("L2", "Q", label);
    assert(!strcmp(label, "RMB"));
    assert(VirtualFree(vm_base, 0, MEM_RELEASE));
    std::filesystem::remove(archive);
    puts("Actual Windows menu source components passed; no game assets used.");
}

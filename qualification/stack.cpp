#include <windows.h>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
uint8_t* vm_base;
#include "ppu_stack_read.h"
int main()
{
    SYSTEM_INFO info; GetSystemInfo(&info);
    const size_t page = info.dwPageSize;
    vm_base = static_cast<uint8_t*>(VirtualAlloc(nullptr, page * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    assert(vm_base);
    const uint32_t expected = 0x12345678;
    memcpy(vm_base + page - 4, &expected, sizeof expected);
    DWORD old;
    assert(VirtualProtect(vm_base + page, page, PAGE_NOACCESS, &old));
    uint32_t word = 0;
    assert(racit_stack_read(static_cast<uint32_t>(page - 4), &word) && word == expected);
    assert(!racit_stack_read(static_cast<uint32_t>(page), &word));
    assert(!racit_stack_read(static_cast<uint32_t>(page - 2), &word));
    assert(!racit_stack_read(0xffffffffu, &word));
    assert(VirtualFree(vm_base, 0, MEM_RELEASE));
    puts("Actual Windows reader passed four valid/protected/cross-page/overflow cases.");
}

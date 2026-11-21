#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

BOOL WINAPI ReadFileFixture(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);

#define ReadFile ReadFileFixture
#include "../launcher/src/util.cpp"
#undef ReadFile

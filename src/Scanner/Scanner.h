#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstddef>
#include <cstdint>

namespace Scanner
{

    uintptr_t ScanRange(uintptr_t start, size_t size, const char* signature);

    uintptr_t ScanModule(const wchar_t* module, const char* signature);

    uintptr_t ScanMainMod(const char* signature);

    uintptr_t ResolveRelative(uintptr_t instruction, int offset = 1, int instrSize = 5);

    uintptr_t ScanCallTarget(const char* signature, int relOffset = 1, int instrSize = 5);
}

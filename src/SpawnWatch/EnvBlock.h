#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace EnvBlock
{

    wchar_t* AddOrReplace(const wchar_t* src, const wchar_t* name, const wchar_t* value);

    void Free(wchar_t* block);
}

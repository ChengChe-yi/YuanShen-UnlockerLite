#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstddef>

namespace ProcessInfo
{
    struct MainModule
    {
        uintptr_t base    = 0;
        size_t    size    = 0;
        uint16_t  machine = 0;
        wchar_t   path[MAX_PATH] = {};
        wchar_t   version[64]    = {};
    };

    struct SelfModule
    {
        HMODULE handle = nullptr;
        wchar_t path[MAX_PATH] = {};
        wchar_t dir[MAX_PATH]  = {};
    };

    struct Snapshot
    {
        DWORD      pid       = 0;
        DWORD      tid       = 0;
        bool       wow64     = false;
        uint64_t   startTime = 0;
        MainModule main      = {};
        SelfModule self      = {};
    };

    bool Capture(HMODULE selfHandle = nullptr);

    bool SelfSiblingPath(const wchar_t* ext, wchar_t* out, size_t outChars);

    const Snapshot& Get();
    void Log();
}

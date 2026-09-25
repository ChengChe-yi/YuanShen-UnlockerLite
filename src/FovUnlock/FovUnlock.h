#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "Hooks.h"

namespace FovUnlock
{
    using FnSetFieldOfView = void(__fastcall*)(void* self, float value);

    Hooks::Hook<FnSetFieldOfView>& SetFieldOfViewHook();

    void __fastcall DetourSetFieldOfView(void* self, float value);

    bool Init();

    void Apply();

    void Uninit();
}
extern "C" __declspec(dllexport) int SetFov(int fov);

extern "C" __declspec(dllexport) int GetFov(void);

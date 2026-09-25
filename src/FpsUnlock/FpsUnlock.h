#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>


namespace FpsUnlock
{

    bool Init();

    void Apply();

    void Uninit();
}


extern "C" __declspec(dllexport) int SetFps(int fps);

extern "C" __declspec(dllexport) int GetFps(void);

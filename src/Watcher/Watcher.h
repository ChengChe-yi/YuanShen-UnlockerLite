#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace Watcher
{

    bool Start(const wchar_t* filePath, void (*onChange)());

    void Stop();
}

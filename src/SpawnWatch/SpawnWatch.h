#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "Hooks.h"

namespace SpawnWatch
{
    using FnCreateProcessW = BOOL(WINAPI*)(LPCWSTR lpApplicationName,
                                           LPWSTR lpCommandLine,
                                           LPSECURITY_ATTRIBUTES lpProcessAttributes,
                                           LPSECURITY_ATTRIBUTES lpThreadAttributes,
                                           BOOL bInheritHandles,
                                           DWORD dwCreationFlags,
                                           LPVOID lpEnvironment,
                                           LPCWSTR lpCurrentDirectory,
                                           LPSTARTUPINFOW lpStartupInfo,
                                           LPPROCESS_INFORMATION lpProcessInformation);

    Hooks::Hook<FnCreateProcessW>& CreateProcessWHook();

    BOOL WINAPI DetourCreateProcessW(LPCWSTR lpApplicationName,
                                     LPWSTR lpCommandLine,
                                     LPSECURITY_ATTRIBUTES lpProcessAttributes,
                                     LPSECURITY_ATTRIBUTES lpThreadAttributes,
                                     BOOL bInheritHandles,
                                     DWORD dwCreationFlags,
                                     LPVOID lpEnvironment,
                                     LPCWSTR lpCurrentDirectory,
                                     LPSTARTUPINFOW lpStartupInfo,
                                     LPPROCESS_INFORMATION lpProcessInformation);
}

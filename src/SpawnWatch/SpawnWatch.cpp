#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "SpawnWatch.h"
#include "EnvBlock.h"
#include "Logger.h"
#include <cstring>
#include <cwchar>

namespace SpawnWatch
{
    static Hooks::Hook<FnCreateProcessW> g_createProcessW;

    static const wchar_t kBrowserImage[] = L"ZFGameBrowser.exe";

    static constexpr wchar_t kCompatLayerVar[]   = L"__COMPAT_LAYER";
    static constexpr wchar_t kCompatLayerValue[] = L"VISTASP2";

    static constexpr size_t kFieldBytes = 512;
    static constexpr size_t kMaxWideChars = 160;

    static void FirstToken(const wchar_t* cmdLine, wchar_t* out, size_t outChars)
    {
        out[0] = 0;
        if (!cmdLine || !*cmdLine || outChars == 0)
            return;

        const wchar_t* p = cmdLine;
        while (*p == L' ' || *p == L'\t')
            ++p;

        const wchar_t* start = p;
        if (*p == L'"') {
            start = ++p;
            while (*p && *p != L'"')
                ++p;
        } else {
            while (*p && *p != L' ' && *p != L'\t')
                ++p;
        }

        size_t n = static_cast<size_t>(p - start);
        if (n >= outChars)
            n = outChars - 1;

        wcsncpy_s(out, outChars, start, n);
    }

    static void ImageName(const wchar_t* appName, const wchar_t* cmdLine,
                          wchar_t* out, size_t outChars)
    {
        if (appName && *appName)
            wcsncpy_s(out, outChars, appName, _TRUNCATE);
        else
            FirstToken(cmdLine, out, outChars);
    }

    static bool IsBrowserImage(const wchar_t* image)
    {
        if (!image || !*image)
            return false;

        const wchar_t* base = wcsrchr(image, L'\\');
        base = base ? base + 1 : image;
        return _wcsicmp(base, kBrowserImage) == 0;
    }

    static void ToUtf8(const wchar_t* src, char* out, size_t outBytes, size_t maxChars)
    {
        if (outBytes == 0)
            return;
        out[0] = 0;
        if (!src || !*src)
            return;

        wchar_t tmp[kMaxWideChars + 1] = {};
        size_t n = 0;
        while (n < maxChars && n < kMaxWideChars && src[n]) {
            tmp[n] = src[n];
            ++n;
        }
        tmp[n] = 0;

        if (WideCharToMultiByte(CP_UTF8, 0, tmp, -1, out,
                                static_cast<int>(outBytes), nullptr, nullptr) <= 0)
            out[0] = 0;
    }

    static void RealOsVersion(DWORD* major, DWORD* minor)
    {
        *major = 0;
        *minor = 0;

        if (const HMODULE nt = GetModuleHandleW(L"ntdll.dll")) {
            using FnRtlGetVersion = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
            const FARPROC proc = GetProcAddress(nt, "RtlGetVersion");
            if (proc) {
#pragma warning(push)
#pragma warning(disable : 4191)
                const auto fn = reinterpret_cast<FnRtlGetVersion>(proc);
#pragma warning(pop)
                RTL_OSVERSIONINFOW vi = {};
                vi.dwOSVersionInfoSize = sizeof(vi);
                if (fn(&vi) == 0) {
                    *major = vi.dwMajorVersion;
                    *minor = vi.dwMinorVersion;
                    return;
                }
            }
        }

        OSVERSIONINFOW vi = {};
        vi.dwOSVersionInfoSize = sizeof(vi);
#pragma warning(suppress : 4996)
        if (GetVersionExW(&vi)) {
            *major = vi.dwMajorVersion;
            *minor = vi.dwMinorVersion;
        }
    }

    static bool CompatLayerNeeded()
    {
        static const bool needed = [] {
            DWORD major = 0, minor = 0;
            RealOsVersion(&major, &minor);
            return (major > 6) || (major == 6 && minor >= 2);
        }();
        return needed;
    }

    static wchar_t* TryBuildCompatEnv(LPVOID lpEnvironment, DWORD dwCreationFlags)
    {
        wchar_t* built = nullptr;

        __try {
            const wchar_t* srcEnv = nullptr;
            bool ownSrc = false;

            if (!lpEnvironment) {
                srcEnv = GetEnvironmentStringsW();
                ownSrc = true;
            } else if (dwCreationFlags & CREATE_UNICODE_ENVIRONMENT) {
                srcEnv = static_cast<const wchar_t*>(lpEnvironment);
            } else {
                LOG_MSG("Spawn", "浏览器拿到的是 ANSI 环境块，跳过兼容层注入（原样放行）");
                return nullptr;
            }

            if (!srcEnv) {
                LOG_MSG("Spawn", "取不到环境块，原样放行");
                return nullptr;
            }

            built = EnvBlock::AddOrReplace(srcEnv, kCompatLayerVar, kCompatLayerValue);
            if (!built)
                LOG_MSG("Spawn", "构造新环境块失败（格式不合法或内存不足），原样放行");

            if (ownSrc)
                FreeEnvironmentStringsW(const_cast<LPWSTR>(srcEnv));
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            LOG("Spawn", "读环境块时发生异常（0x%08lX），放弃注入、原样放行", GetExceptionCode());
            EnvBlock::Free(built);
            built = nullptr;
        }

        return built;
    }

    Hooks::Hook<FnCreateProcessW>& CreateProcessWHook() { return g_createProcessW; }

    BOOL WINAPI DetourCreateProcessW(LPCWSTR lpApplicationName,
                                     LPWSTR lpCommandLine,
                                     LPSECURITY_ATTRIBUTES lpProcessAttributes,
                                     LPSECURITY_ATTRIBUTES lpThreadAttributes,
                                     BOOL bInheritHandles,
                                     DWORD dwCreationFlags,
                                     LPVOID lpEnvironment,
                                     LPCWSTR lpCurrentDirectory,
                                     LPSTARTUPINFOW lpStartupInfo,
                                     LPPROCESS_INFORMATION lpProcessInformation)
    {
        HOOK_INFLIGHT_SCOPE();

        LPVOID   envToPass   = lpEnvironment;
        DWORD    flagsToPass = dwCreationFlags;
        wchar_t* newEnv      = nullptr;

        wchar_t image[MAX_PATH] = {};
        ImageName(lpApplicationName, lpCommandLine, image, ARRAYSIZE(image));

        if (IsBrowserImage(image)) {

            char shown[kFieldBytes] = {};
            ToUtf8(image, shown, sizeof(shown), kMaxWideChars);
            LOG("Spawn", "命中目标进程：%s", shown[0] ? shown : "(路径取不到)");

            if (!CompatLayerNeeded()) {
                DWORD major = 0, minor = 0;
                RealOsVersion(&major, &minor);
                LOG("Spawn", "系统 %lu.%lu 早于 Windows 8（本来就没有 DirectComposition），"
                             "跳过注入、原样放行", major, minor);
            } else {

                newEnv = TryBuildCompatEnv(lpEnvironment, dwCreationFlags);
                if (newEnv) {
                    envToPass = newEnv;

                    flagsToPass |= CREATE_UNICODE_ENVIRONMENT;
                    LOG("Spawn", "已给浏览器进程注入 %ls=%ls（其子进程会一并继承）",
                        kCompatLayerVar, kCompatLayerValue);
                }
            }
        }

        FnCreateProcessW original = g_createProcessW.Original();
        if (!original) {
            EnvBlock::Free(newEnv);
            return FALSE;
        }

        const BOOL ok = original(lpApplicationName, lpCommandLine,
                                 lpProcessAttributes, lpThreadAttributes,
                                 bInheritHandles, flagsToPass,
                                 envToPass, lpCurrentDirectory,
                                 lpStartupInfo, lpProcessInformation);

        EnvBlock::Free(newEnv);
        return ok;
    }
}

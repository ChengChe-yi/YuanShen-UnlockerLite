#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "FpsUnlock.h"
#include "Config.h"
#include "Game.h"
#include "Logger.h"
#include "Patterns.h"
#include "ProcessInfo.h"
#include "Scanner.h"
#include <atomic>

namespace FpsUnlock
{

    static volatile int* g_fpsAddr = nullptr;

    static std::atomic<int> g_target{ 0 };

    static std::atomic<bool> g_enabled{ false };

    static constexpr DWORD kWriteIntervalMs = 500;

    static HANDLE g_thread = nullptr;
    
    static HANDLE g_stop = nullptr;

    static constexpr DWORD kThreadStopWaitMs = 3000;

    static int NormalizeTarget(int fps)
    {
        if (fps <= 0)
            return 0;

        if (fps > 1000)
            LOG("Fps", "目标 %d 超过游戏硬上限 1000（sub_1416A6790 里的 fminf），实际只会有 1000", fps);

        return fps;
    }

    static uintptr_t ResolveFpsGlobalBySig()
    {
        const uintptr_t hit = Scanner::ScanMainMod(Patterns::Sig::FpsReadInFramePacer);
        if (!hit) {
            LOG_MSG("Fps", "帧率读取指令的特征码未命中");
            return 0;
        }

        const uintptr_t target = Scanner::ResolveRelative(hit, 4, 8);
        if (!target) {
            LOG("Fps", "特征码命中 %p，但 disp32 取不出来（内存不可读）",
                reinterpret_cast<void*>(hit));
            return 0;
        }

        const ProcessInfo::MainModule& main = ProcessInfo::Get().main;
        if (target < main.base || target >= main.base + main.size) {
            LOG("Fps", "签名反推出 %p，落在主模块 [%p, %p) 之外，判为不可信",
                reinterpret_cast<void*>(target),
                reinterpret_cast<void*>(main.base),
                reinterpret_cast<void*>(main.base + main.size));
            return 0;
        }

        LOG("Fps", "特征码命中 %p，反推出帧率全局 %p",
            reinterpret_cast<void*>(hit), reinterpret_cast<void*>(target));
        return target;
    }

    static uintptr_t ResolveFpsGlobal()
    {
        const uintptr_t fromSig = ResolveFpsGlobalBySig();
        const uintptr_t fromRva = Game::Resolve(Patterns::Rva::FpsLimit);

        if (!fromSig) {
            if (fromRva)
                LOG_MSG("Fps", "退回硬编码 RVA 兜底（签名未命中：可能游戏改了那段代码的形状，"
                               "也可能只是这条签名过时）。写入前仍会做可写性校验");
            return fromRva;
        }

        if (fromRva && fromSig != fromRva)
            LOG("Fps", "签名反推出的 %p 与硬编码 RVA 推出的 %p 不一致 —— 游戏更新过，"
                       "RVA 已过期。本次按签名走；核对无误后请更新 Patterns::Rva::FpsLimit",
                reinterpret_cast<void*>(fromSig), reinterpret_cast<void*>(fromRva));

        return fromSig;
    }

    static bool ResolveTarget()
    {
        const uintptr_t addr = ResolveFpsGlobal();
        if (!addr) {
            LOG_MSG("Fps", "帧率全局定位失败（签名与 RVA 都没给出可用地址），放弃写入");
            return false;
        }

        g_fpsAddr = Game::AcquireWritable<int>(addr);
        if (!g_fpsAddr) {
            MEMORY_BASIC_INFORMATION mbi{};
            Game::Query(addr, mbi);
            LOG("Fps", "目标 %p 不可写（State=0x%lX Protect=0x%lX），多半是游戏版本不匹配，放弃写入",
                reinterpret_cast<void*>(addr),
                static_cast<unsigned long>(mbi.State),
                static_cast<unsigned long>(mbi.Protect));
            return false;
        }

        LOG("Fps", "帧率全局 %p（主模块基址 %p + 0x%llX）",
            reinterpret_cast<void*>(addr),
            reinterpret_cast<void*>(Game::ModuleBase()),
            static_cast<unsigned long long>(addr - Game::ModuleBase()));
        return true;
    }

    static void WriteOnce()
    {
        if (!g_fpsAddr)
            return;

        *g_fpsAddr = g_target.load(std::memory_order_relaxed);
    }

    static DWORD WINAPI WriterProc(LPVOID)
    {
        LOG("Fps", "覆写线程启动：目标 %d（间隔每轮重读配置）",
            g_target.load(std::memory_order_relaxed));

        for (;;) {

            if (g_enabled.load(std::memory_order_relaxed))
                WriteOnce();

            if (WaitForSingleObject(g_stop, kWriteIntervalMs) == WAIT_OBJECT_0)
                break;
        }

        LOG_MSG("Fps", "覆写线程退出");
        return 0;
    }

    static bool EnsureWriterThread()
    {
        if (g_thread)
            return true;

        if (!g_fpsAddr) {
            LOG_MSG("Fps", "帧率全局地址未解析到，无法开始覆写");
            return false;
        }

        g_stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!g_stop) {
            LOG("Fps", "CreateEvent 失败（错误 %lu）", GetLastError());
            return false;
        }

        g_thread = CreateThread(nullptr, 0, WriterProc, nullptr, 0, nullptr);
        if (!g_thread) {
            LOG("Fps", "CreateThread 失败（错误 %lu）", GetLastError());
            CloseHandle(g_stop);
            g_stop = nullptr;
            return false;
        }
        return true;
    }

    void Apply()
    {
        const Config::Values cfg = Config::Snapshot();

        g_target.store(NormalizeTarget(cfg.targetFps), std::memory_order_relaxed);
        g_enabled.store(cfg.fpsEnabled, std::memory_order_relaxed);

        if (!cfg.fpsEnabled) {
            LOG_MSG("Fps", "配置里已关闭：停止周期性覆写");
            return;
        }

        if (!EnsureWriterThread())
            return;

        WriteOnce();

        LOG("Fps", "已应用：目标 %d fps，覆写间隔 %lu ms",
            g_target.load(std::memory_order_relaxed), kWriteIntervalMs);
    }

    bool Init()
    {

        if (!ResolveTarget())
            return false;

        Apply();
        return true;
    }

    void Uninit()
    {
        if (g_stop) {
            SetEvent(g_stop);
            if (g_thread) {
                if (WaitForSingleObject(g_thread, kThreadStopWaitMs) != WAIT_OBJECT_0)
                    LOG("Fps", "覆写线程未在 %lu ms 内退出", kThreadStopWaitMs);
                CloseHandle(g_thread);
                g_thread = nullptr;
            }
            CloseHandle(g_stop);
            g_stop = nullptr;
        }

    }
}

extern "C" __declspec(dllexport) int SetFps(int fps)
{
    const int v = (fps <= 0) ? 0 : fps;
    FpsUnlock::g_target.store(v, std::memory_order_relaxed);
    FpsUnlock::WriteOnce();
    return FpsUnlock::g_fpsAddr ? 1 : 0;
}

extern "C" __declspec(dllexport) int GetFps(void)
{
    return FpsUnlock::g_target.load(std::memory_order_relaxed);
}

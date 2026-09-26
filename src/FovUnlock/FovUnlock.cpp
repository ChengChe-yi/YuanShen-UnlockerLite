#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "FovUnlock.h"
#include "Config.h"
#include "Logger.h"
#include <atomic>

namespace FovUnlock
{
    static Hooks::Hook<FnSetFieldOfView> g_hook;
    static std::atomic<int>   g_target{ 60 };
    static std::atomic<float> g_scale{ 1.0f };
    static std::atomic<bool>  g_active{ false };

    static constexpr float kPassThroughFov = 40.0f;
    static constexpr float kGameDefaultFov = 45.0f;
    static constexpr int kMinFov = 45;
    static constexpr int kMaxFov = 179;

    static int ClampFov(int fov)
    {
        if (fov < kMinFov) return kMinFov;
        if (fov > kMaxFov) return kMaxFov;
        return fov;
    }

    Hooks::Hook<FnSetFieldOfView>& SetFieldOfViewHook() { return g_hook; }

    void __fastcall DetourSetFieldOfView(void* self, float value)
    {
        HOOK_INFLIGHT_SCOPE();

        if (g_active.load(std::memory_order_relaxed)) {
            const float target = static_cast<float>(g_target.load(std::memory_order_relaxed));

            if (value > kPassThroughFov && value <= kGameDefaultFov) {
                value = kPassThroughFov +
                        (value - kPassThroughFov) * g_scale.load(std::memory_order_relaxed);
            }
            else if (value > kGameDefaultFov && value <= target) {
                value = target;
            }

        }

        if (FnSetFieldOfView original = g_hook.Original())
            original(self, value);
    }

    // 把当前配置搬到运行态。Init 与热重载共用这一处 ——
    // 重载后 FOV 会立刻按新值工作，不需要重新注入。
    void Apply()
    {
        const Config::Values cfg = Config::Snapshot();

        const int target = ClampFov(cfg.targetFov);
        g_target.store(target, std::memory_order_relaxed);

        // 映射段 (kPassThroughFov, kGameDefaultFov] 的斜率：把 gameDefault 顶到 target。
        // 段在 kPassThroughFov 处与透传接上（值相等）、在 kGameDefaultFov 处与
        // 钳到 target 的段接上，所以两处接缝都连续。
        g_scale.store((static_cast<float>(target) - kPassThroughFov) /
                      (kGameDefaultFov - kPassThroughFov),
                      std::memory_order_relaxed);

        g_active.store(cfg.fovEnabled, std::memory_order_relaxed);

        LOG("Fov", "已应用：接管=%d 目标 %.0f（透传上限 %.0f，游戏上限 %.0f）",
            cfg.fovEnabled ? 1 : 0, static_cast<double>(target),
            static_cast<double>(kPassThroughFov), static_cast<double>(kGameDefaultFov));
    }

    bool Init()
    {
        if (!g_hook.Installed()) {
            LOG_MSG("Fov", "hook 未安装（特征码未命中或安装失败），无法接管");
            return false;
        }

        Apply();
        return true;
    }

    void Uninit()
    {
        g_active.store(false, std::memory_order_relaxed);
        g_hook.Detach();
    }
}

extern "C" __declspec(dllexport) int SetFov(int fov)
{
    FovUnlock::g_target.store(FovUnlock::ClampFov(fov), std::memory_order_relaxed);
    return 1;
}

extern "C" __declspec(dllexport) int GetFov(void)
{
    return FovUnlock::g_target.load(std::memory_order_relaxed);
}

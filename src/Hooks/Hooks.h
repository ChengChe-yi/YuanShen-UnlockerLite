#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <MinHook.h>
#include <cstdint>
#include <type_traits>
#include <utility>
#include "Game.h"
#include "Logger.h"
#include "Scanner.h"

namespace Hooks
{

    bool Init();

    void Uninit();

    // ---- 全局批量操作（作用于所有已创建 hook）-------------------------------

    bool EnableAll();    // MH_EnableHook(MH_ALL_HOOKS)
    bool DisableAll();   // MH_DisableHook(MH_ALL_HOOKS)
    void RemoveAll();    // MH_RemoveHook(MH_ALL_HOOKS)

    // 把之前 QueueEnable() 排队的项一次性启用（内部冻结线程打补丁）。
    bool ApplyQueued();  // MH_ApplyQueued()

    // ---- 热卸载支持 ---------------------------------------------------------

    // 在途调用计数：detour 进入 +1、退出 -1。热卸载靠它判断"现在还有几个线程
    // 停在 detour 里"。计数由 detour 里的 HOOK_INFLIGHT_SCOPE() 维护。
    namespace InFlight
    {
        void Enter() noexcept;
        void Leave() noexcept;
        long Count() noexcept;

        // 等待计数归零；超时返回 false（调用方自行决定是否硬着头皮继续）。
        bool WaitEmpty(DWORD timeoutMs);

        // RAII：进入 +1，析构 -1。用 HOOK_INFLIGHT_SCOPE() 声明。
        struct Scope
        {
            Scope() noexcept { Enter(); }
            ~Scope() noexcept { Leave(); }
            Scope(const Scope&)            = delete;
            Scope& operator=(const Scope&) = delete;
        };
    }

    namespace Detail
    {
        // 幂等初始化 MinHook；Hook<Fn> 内部会调用，无需手工调。
        bool EnsureMinHook();
    }

    // ------------------------------------------------------------------------
    // Hook<Fn> —— 类型安全的 hook 句柄。
    // ------------------------------------------------------------------------
    template <typename Fn>
    class Hook
    {
        static_assert(std::is_pointer_v<Fn>,
                      "Hook<Fn> 需要函数指针类型，例如 Hook<int(__fastcall*)(int)>");
        static_assert(std::is_function_v<std::remove_pointer_t<Fn>>,
                      "Fn 必须是指向函数的指针");

    public:
        Hook() = default;
        ~Hook() { Uninstall(); }

        Hook(const Hook&)            = delete;
        Hook& operator=(const Hook&) = delete;

        Hook(Hook&& other) noexcept { Take(other); }
        Hook& operator=(Hook&& other) noexcept
        {
            if (this != &other) {
                Uninstall();
                Take(other);
            }
            return *this;
        }

        // ---- 一次性安装（Create + Enable），适合单个 hook -------------------

        // target 为绝对地址，detour 为与目标同签名的函数。
        // 已安装时先 Uninstall()，因此可重复调用。
        bool Install(void* target, Fn detour);

        // target 由「模块基址 + RVA」解析；module 为 nullptr 时用主模块。
        bool InstallRva(uintptr_t rva, Fn detour, const wchar_t* module = nullptr);

        // ---- 原子安装（先全部 Prepare，再统一生效）-------------------------

        // 只创建不启用。配合 QueueEnable() + Hooks::ApplyQueued() 使用：
        // 所有 hook 都 Prepare 成功后才统一打补丁，任一失败就整体回滚。
        // 逆向地址因游戏更新失效时，这个"要么全装要么全不装"很重要。
        bool Prepare(void* target, Fn detour);
        bool PrepareRva(uintptr_t rva, Fn detour, const wchar_t* module = nullptr);

        // 排队等待启用；真正生效在随后的 Hooks::ApplyQueued()。
        bool QueueEnable();

        // ---- 停止 / 卸载 ----------------------------------------------------

        // 停止拦截但保留 trampoline，Original() 仍然有效。热卸载的第一步。
        bool Detach();

        // 彻底卸载（释放 trampoline，Original() 随之失效）。幂等。
        void Uninstall();

        // 停止/恢复拦截，句柄与 trampoline 都保留，Original() 始终有效。
        // 调试期用这个做开关，不要用 Uninstall()。
        bool Enable();
        bool Disable();

        // ---- 状态 -----------------------------------------------------------

        bool Installed() const noexcept { return m_created; }
        bool Enabled()   const noexcept { return m_enabled; }
        explicit operator bool() const noexcept { return m_created; }

        // 原函数指针。Uninstall() 之后失效，Detach()/Disable() 之后仍有效。
        Fn    Original() const noexcept { return m_original; }
        void* Target()   const noexcept { return m_target; }

        // 最近一次 MinHook 调用的状态码。
        MH_STATUS LastStatus() const noexcept { return m_status; }

    private:
        void Take(Hook& other) noexcept
        {
            m_detour   = other.m_detour;
            m_target   = other.m_target;
            m_original = other.m_original;
            m_status   = other.m_status;
            m_created  = other.m_created;
            m_enabled  = other.m_enabled;

            other.m_detour   = nullptr;
            other.m_target   = nullptr;
            other.m_original = nullptr;
            other.m_status   = MH_OK;
            other.m_created  = false;
            other.m_enabled  = false;
        }

        Fn        m_detour   = nullptr;
        void*     m_target   = nullptr;
        Fn        m_original = nullptr;
        MH_STATUS m_status   = MH_OK;
        bool      m_created  = false;
        bool      m_enabled  = false;
    };

    template <typename Fn>
    bool Hook<Fn>::Install(void* target, Fn detour)
    {
        if (!Prepare(target, detour))
            return false;

        if (!Enable()) {

            MH_RemoveHook(m_target);
            m_target  = nullptr;
            m_detour  = nullptr;
            m_created = false;
            m_enabled = false;
            return false;
        }
        return true;
    }

    template <typename Fn>
    bool Hook<Fn>::InstallRva(uintptr_t rva, Fn detour, const wchar_t* module)
    {
        if (!PrepareRva(rva, detour, module))
            return false;

        if (!Enable()) {
            MH_RemoveHook(m_target);
            m_target  = nullptr;
            m_detour  = nullptr;
            m_created = false;
            m_enabled = false;
            return false;
        }
        return true;
    }

    template <typename Fn>
    bool Hook<Fn>::Prepare(void* target, Fn detour)
    {
        Uninstall();

        if (!target || !detour) {
            m_status = MH_ERROR_NOT_CREATED;
            LOG("Hooks", "准备失败：target=%p detour=%p（有空值）", target, (void*)detour);
            return false;
        }

        if (!Detail::EnsureMinHook()) {
            m_status = MH_ERROR_NOT_INITIALIZED;
            LOG_MSG("Hooks", "准备失败：MinHook 初始化失败");
            return false;
        }

        void* original = nullptr;
#pragma warning(push)
#pragma warning(disable : 4191)
        m_status = MH_CreateHook(target, reinterpret_cast<LPVOID>(detour), &original);
#pragma warning(pop)
        if (m_status != MH_OK) {
            LOG("Hooks", "MH_CreateHook(%p) 失败：%s", target, MH_StatusToString(m_status));
            return false;
        }

        m_target = target;
        m_detour = detour;
#pragma warning(push)
#pragma warning(disable : 4191)
        m_original = reinterpret_cast<Fn>(original);
#pragma warning(pop)
        m_created = true;
        m_enabled = false;

        LOG("Hooks", "已创建待启用 %p -> detour %p（原函数 %p）", target, (void*)detour, original);
        return true;
    }

    template <typename Fn>
    bool Hook<Fn>::PrepareRva(uintptr_t rva, Fn detour, const wchar_t* module)
    {
        void* target = reinterpret_cast<void*>(Game::Resolve(rva, module));
        if (!target) {
            m_status = MH_ERROR_NOT_CREATED;
            LOG("Hooks", "准备失败：模块未加载，RVA 0x%llX 解析不到地址",
                static_cast<unsigned long long>(rva));
            return false;
        }
        return Prepare(target, detour);
    }

    template <typename Fn>
    bool Hook<Fn>::QueueEnable()
    {
        if (!m_created) {
            LOG_MSG("Hooks", "排队启用失败：hook 尚未创建（要先 Prepare）");
            return false;
        }

        m_status = MH_QueueEnableHook(m_target);
        if (m_status != MH_OK) {
            LOG("Hooks", "MH_QueueEnableHook(%p) 失败：%s", m_target, MH_StatusToString(m_status));
            return false;
        }

        m_enabled = true;
        return true;
    }

    template <typename Fn>
    bool Hook<Fn>::Detach()
    {
        if (!m_created) {
            LOG_MSG("Hooks", "停止拦截失败：hook 未创建");
            return false;
        }
        if (!m_enabled)
            return true;   // 已经停了

        m_status = MH_DisableHook(m_target);
        if (m_status != MH_OK) {
            LOG("Hooks", "MH_DisableHook(%p) 失败：%s", m_target, MH_StatusToString(m_status));
            return false;
        }
        m_enabled = false;

        LOG("Hooks", "已停止拦截 %p（trampoline 保留）", m_target);
        return true;
    }

    template <typename Fn>
    void Hook<Fn>::Uninstall()
    {
        if (!m_created)
            return;

        MH_DisableHook(m_target);             // 先恢复原始字节
        m_status = MH_RemoveHook(m_target);   // 再释放 trampoline

        LOG("Hooks", "已卸载 %p（%s）", m_target, MH_StatusToString(m_status));

        m_target  = nullptr;
        m_detour  = nullptr;
        m_created = false;
        m_enabled = false;
    }

    template <typename Fn>
    bool Hook<Fn>::Enable()
    {
        if (!m_created) {
            LOG_MSG("Hooks", "启用失败：hook 未创建");
            return false;
        }

        m_status = MH_EnableHook(m_target);
        if (m_status != MH_OK) {
            LOG("Hooks", "MH_EnableHook(%p) 失败：%s", m_target, MH_StatusToString(m_status));
            return false;
        }
        m_enabled = true;
        return true;
    }

    template <typename Fn>
    bool Hook<Fn>::Disable()
    {
        if (!m_created) {
            LOG_MSG("Hooks", "停用失败：hook 未创建");
            return false;
        }

        m_status = MH_DisableHook(m_target);
        if (m_status != MH_OK) {
            LOG("Hooks", "MH_DisableHook(%p) 失败：%s", m_target, MH_StatusToString(m_status));
            return false;
        }
        m_enabled = false;
        return true;
    }

    template <typename Fn>
    bool Install(Hook<Fn>& hook, Fn detour, const char* name,
                 const char* signature, uintptr_t rva)
    {
        const char* tag = name ? name : "(未命名)";
        (void)tag;

        uintptr_t addr = 0;
        if (signature && signature[0]) {
            addr = Scanner::ScanMainMod(signature);
            if (!addr) {
                LOG("Hooks", "%s：特征码未命中（游戏更新过？），放弃安装", tag);
                return false;
            }
        } else {
            addr = Game::Resolve(rva);
            if (!addr) {
                LOG("Hooks", "%s：RVA 0x%llX 解析失败，放弃安装", tag,
                    static_cast<unsigned long long>(rva));
                return false;
            }
        }

        return hook.Install(reinterpret_cast<void*>(addr), detour);
    }

    template <typename Fn>
    bool InstallExport(Hook<Fn>& hook, Fn detour, const char* name,
                       const wchar_t* moduleName, const char* exportName)
    {
        const char* tag = name ? name : "(未命名)";
        (void)tag;

        const HMODULE mod = GetModuleHandleW(moduleName);
        if (!mod) {
            LOG("Hooks", "%s：模块 %ls 未加载，放弃安装", tag, moduleName);
            return false;
        }

        void* target = reinterpret_cast<void*>(GetProcAddress(mod, exportName));
        if (!target) {
            LOG("Hooks", "%s：%ls!%s 取不到地址，放弃安装", tag, moduleName, exportName);
            return false;
        }

        return hook.Install(target, detour);
    }

    template <typename Fn>
    bool InstallCallTarget(Hook<Fn>& hook, Fn detour, const char* name,
                           const char* callSignature, uintptr_t fallbackRva)
    {
        const char* tag = name ? name : "(未命名)";

        uintptr_t addr = 0;
        if (callSignature && callSignature[0]) {
            const uintptr_t hit = Scanner::ScanMainMod(callSignature);
            if (hit)
                addr = Scanner::ResolveRelative(hit, 1, 5);

            if (!addr)
                LOG("Hooks", "%s：调用点签名未命中或 rel32 解析失败，改用 RVA 兜底", tag);
        }

        if (!addr)
            addr = Game::Resolve(fallbackRva);

        if (!addr) {
            LOG("Hooks", "%s：签名与 RVA 都没给出地址，放弃安装", tag);
            return false;
        }

        return hook.Install(reinterpret_cast<void*>(addr), detour);
    }
}

#define HOOK_INFLIGHT_SCOPE() ::Hooks::InFlight::Scope hook_inflight_scope_

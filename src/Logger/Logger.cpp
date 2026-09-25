#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "Logger.h"
#include "ProcessInfo.h"
#include <cstring>

namespace Logger
{
    std::atomic<bool> g_logWriteEnabled{ true };

    // g_logPath / g_logPathReady 仅在 g_logLock 保护下读写：
    // readiness 判断也必须在锁内做，否则 CloseLog 之后排队进入的写者
    // 会因只查 g_logFile==nullptr 而重新 _wfopen_s，让已关闭的日志复活。
    static wchar_t g_logPath[MAX_PATH] = {};
    static bool    g_logPathReady = false;
    static SRWLOCK g_logLock = SRWLOCK_INIT;

    // 句柄缓存：频繁写入期间保持文件打开，省去每行的 open/close；
    // 空闲超过 kIdleCloseMs 由线程池定时器关闭，句柄释放后日志文件可被删除/替换。
    static constexpr DWORD kIdleCloseMs = 1000;

    static FILE*     g_logFile = nullptr;   // 缓存中的句柄（nullptr = 已关闭）
    static ULONGLONG g_lastWriteTick = 0;
    static PTP_TIMER g_idleTimer = nullptr;

    // 负值 FILETIME = 相对当前时刻的到期时间（单位 100ns）。
    static FILETIME RelativeDue(DWORD ms)
    {
        LARGE_INTEGER li;
        li.QuadPart = -(static_cast<LONGLONG>(ms) * 10000LL);
        FILETIME ft;
        ft.dwLowDateTime = li.LowPart;
        ft.dwHighDateTime = li.HighPart;
        return ft;
    }

    // 空闲到期：真空闲就关句柄；期间又有写入则顺延到剩余时间再查。
    static VOID CALLBACK LogIdleCloseCb(PTP_CALLBACK_INSTANCE, PVOID, PTP_TIMER)
    {
        AcquireSRWLockExclusive(&g_logLock);

        if (g_logFile) {
            const ULONGLONG idle = GetTickCount64() - g_lastWriteTick;
            if (idle >= kIdleCloseMs) {
                fclose(g_logFile);
                g_logFile = nullptr;
            }
            else if (g_idleTimer) {
                FILETIME due = RelativeDue(kIdleCloseMs - static_cast<DWORD>(idle));
                SetThreadpoolTimer(g_idleTimer, &due, 0, 0);
            }
        }

        ReleaseSRWLockExclusive(&g_logLock);
    }

    void InitLogFile()
    {
        AcquireSRWLockExclusive(&g_logLock);

        if (!g_logPathReady) {
            if (ProcessInfo::SelfSiblingPath(L".log", g_logPath, ARRAYSIZE(g_logPath)))
                g_logPathReady = true;
        }

        if (g_logPathReady && !g_idleTimer)
            g_idleTimer = CreateThreadpoolTimer(LogIdleCloseCb, nullptr, nullptr);

        ReleaseSRWLockExclusive(&g_logLock);
    }

    void WriteLog(const char* text)
    {
        // 快路径只查原子开关；g_logPathReady 必须在锁内判断（见上方说明），
        // 否则与 CloseLog 存在"先过检查、入锁后复活日志"的竞态。
        if (!g_logWriteEnabled.load(std::memory_order_acquire))
            return;

        AcquireSRWLockExclusive(&g_logLock);

        // CloseLog 已执行（卸载/退出收尾）则丢弃后续写入，不再重开文件。
        if (!g_logPathReady) {
            ReleaseSRWLockExclusive(&g_logLock);
            return;
        }

        // 句柄不在了才重新打开；首次打开空文件时补 BOM。
        if (!g_logFile) {
            _wfopen_s(&g_logFile, g_logPath, L"ab");
            if (g_logFile) {
                fseek(g_logFile, 0, SEEK_END);
                if (ftell(g_logFile) == 0) {
                    const unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
                    fwrite(bom, 1, 3, g_logFile);
                }
            }
        }

        if (g_logFile) {
            fwrite(text, 1, strlen(text), g_logFile);
            // 每行都落盘：崩溃时日志必须存活，这是本日志的主要用途。
            fflush(g_logFile);
            g_lastWriteTick = GetTickCount64();

            // 重置空闲计时：定时器顺延到 kIdleCloseMs 之后。
            if (g_idleTimer) {
                FILETIME due = RelativeDue(kIdleCloseMs);
                SetThreadpoolTimer(g_idleTimer, &due, 0, 0);
            }
        }

        ReleaseSRWLockExclusive(&g_logLock);
    }

    // 仅卸载收尾时调用一次；先在锁外等定时器回调排空，避免死锁。
    void CloseLog()
    {
        if (g_idleTimer) {
            SetThreadpoolTimer(g_idleTimer, nullptr, 0, 0);
            WaitForThreadpoolTimerCallbacks(g_idleTimer, TRUE);
            CloseThreadpoolTimer(g_idleTimer);
            g_idleTimer = nullptr;
        }

        AcquireSRWLockExclusive(&g_logLock);
        if (g_logFile) {
            fclose(g_logFile);
            g_logFile = nullptr;
        }
        g_logPathReady = false;   // 之后到来的零星写入直接丢弃
        ReleaseSRWLockExclusive(&g_logLock);
    }
}

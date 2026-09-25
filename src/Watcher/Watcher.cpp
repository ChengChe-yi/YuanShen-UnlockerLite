#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "Watcher.h"
#include "Logger.h"
#include <cstring>

namespace Watcher
{
    // 编辑器保存不是原子操作（VSCode 临时文件 + rename、记事本截断重写），
    // 一次保存会打出一串通知；等一个安静窗口之后再比 mtime，只认最终那份完整内容。
    static constexpr DWORD kDebounceMs     = 200;
    static constexpr DWORD kPollFallbackMs = 1000;
    static constexpr DWORD kStopWaitMs     = 3000;

    static HANDLE  s_thread   = nullptr;
    static HANDLE  s_stop     = nullptr;
    static wchar_t s_filePath[MAX_PATH] = {};   
    static wchar_t s_dirW[MAX_PATH]     = {};   
    static wchar_t s_fileName[MAX_PATH] = {};   
    static void (*s_onChange)() = nullptr;

    static ULONGLONG s_mtime  = 0;
    static bool      s_seeded = false;

    static ULONGLONG GetMTime(const wchar_t* path)
    {
        HANDLE h = CreateFileW(path, GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            return 0;

        FILETIME ft = {};
        ULONGLONG result = 0;
        if (GetFileTime(h, nullptr, nullptr, &ft))
            result = (static_cast<ULONGLONG>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
        CloseHandle(h);
        return result;
    }

    // 把完整路径拆成「目录（含尾部反斜杠）」+「文件名」。
    static bool SplitPath(const wchar_t* path)
    {
        const size_t len = wcslen(path);
        const wchar_t* lastSlash = wcsrchr(path, L'\\');
        if (!lastSlash)
            return false;

        const size_t dirLen  = static_cast<size_t>(lastSlash - path) + 1;
        const size_t nameLen = len - dirLen;
        if (dirLen == 0 || nameLen == 0)
            return false;

        memcpy(s_dirW, path, dirLen * sizeof(wchar_t));
        s_dirW[dirLen] = 0;
        memcpy(s_fileName, lastSlash + 1, (nameLen + 1) * sizeof(wchar_t));
        return true;
    }

    static wchar_t AsciiLower(wchar_t c)
    {
        return (c >= L'A' && c <= L'Z') ? static_cast<wchar_t>(c + 32) : c;
    }

    static bool NameEqualsI(const wchar_t* a, const wchar_t* b, size_t chars)
    {
        for (size_t i = 0; i < chars; ++i)
            if (AsciiLower(a[i]) != AsciiLower(b[i]))
                return false;
        return true;
    }

    // 这一批通知里是否出现了被监听的文件名。
    // bytes == 0 表示缓冲溢出、内容无法解析 —— 这种情况宁可多查一次。
    static bool BufferMentionsTarget(const BYTE* buf, DWORD bytes)
    {
        if (bytes == 0)
            return true;

        const size_t wantLen = wcslen(s_fileName);
        const BYTE* p   = buf;
        const BYTE* end = buf + bytes;

        while (p + sizeof(FILE_NOTIFY_INFORMATION) <= end) {
            const auto* ni = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(p);
            const size_t nameChars = ni->FileNameLength / sizeof(WCHAR);

            if (nameChars == wantLen && NameEqualsI(ni->FileName, s_fileName, wantLen))
                return true;

            if (ni->NextEntryOffset == 0)
                break;
            p += ni->NextEntryOffset;
        }
        return false;
    }

    static void CheckAndFire()
    {
        const ULONGLONG mt = GetMTime(s_filePath);

        // 首次只播种，不回调 —— 否则启动时就会误报一次「变更」。
        if (!s_seeded) {
            s_mtime  = mt;
            s_seeded = true;
            return;
        }

        if (mt != s_mtime) {
            s_mtime = mt;
            LOG("Watcher", "检测到变更：%ls", s_fileName);
            if (s_onChange)
                s_onChange();
        }
    }

    static DWORD WINAPI ThreadProc(LPVOID)
    {
        LOG_MSG("Watcher", "线程启动");
        CheckAndFire();                       // 播种 mtime

        HANDLE dir = CreateFileW(s_dirW, FILE_LIST_DIRECTORY,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 nullptr, OPEN_EXISTING,
                                 FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
                                 nullptr);

        OVERLAPPED ovl = {};
        ovl.hEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);   // auto-reset

        bool eventDriven = (dir != INVALID_HANDLE_VALUE && ovl.hEvent != nullptr);
        if (!eventDriven)
            LOG_MSG("Watcher", "目录句柄不可用，降级为轮询");

        alignas(DWORD) BYTE buffer[4096];

        for (;;) {
            if (eventDriven) {
                DWORD bytes = 0;
                if (!ReadDirectoryChangesW(dir, buffer, sizeof(buffer), FALSE,
                                           FILE_NOTIFY_CHANGE_LAST_WRITE |
                                               FILE_NOTIFY_CHANGE_FILE_NAME |
                                               FILE_NOTIFY_CHANGE_SIZE,
                                           &bytes, &ovl, nullptr)) {
                    LOG_MSG("Watcher", "ReadDirectoryChangesW 失败，降级为轮询");
                    eventDriven = false;
                    continue;
                }

                HANDLE waits[2] = { s_stop, ovl.hEvent };
                const DWORD r = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
                if (r == WAIT_OBJECT_0 || r == WAIT_FAILED)
                    break;                                  // 停止信号（或句柄失效，兜底退出）
                if (r != WAIT_OBJECT_0 + 1) {
                    LOG("Watcher", "等待返回 %lu，降级为轮询", r);
                    eventDriven = false;
                    continue;
                }


                if (!GetOverlappedResult(dir, &ovl, &bytes, FALSE))
                    bytes = 0;

                if (!BufferMentionsTarget(buffer, bytes))
                    continue;                               


                if (WaitForSingleObject(s_stop, kDebounceMs) == WAIT_OBJECT_0)
                    break;

                CheckAndFire();

            }
            else {
                const DWORD r = WaitForSingleObject(s_stop, kPollFallbackMs);
                if (r == WAIT_OBJECT_0 || r == WAIT_FAILED)
                    break;
                CheckAndFire();
            }
        }

        if (ovl.hEvent) {
            if (dir != INVALID_HANDLE_VALUE)
                CancelIoEx(dir, &ovl);
            CloseHandle(ovl.hEvent);
        }
        if (dir != INVALID_HANDLE_VALUE)
            CloseHandle(dir);

        LOG_MSG("Watcher", "线程退出");
        return 0;
    }

    bool Start(const wchar_t* filePath, void (*onChange)())
    {
        if (s_thread)
            return true;                       

        if (!filePath || !*filePath || !onChange)
            return false;

        if (wcscpy_s(s_filePath, filePath) != 0)
            return false;

        if (!SplitPath(s_filePath)) {
            LOG("Watcher", "路径里取不出目录与文件名：%ls", filePath);
            return false;
        }

        s_onChange = onChange;
        s_seeded   = false;
        s_mtime    = 0;

        s_stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!s_stop)
            return false;

        s_thread = CreateThread(nullptr, 0, ThreadProc, nullptr, 0, nullptr);
        if (!s_thread) {
            CloseHandle(s_stop);
            s_stop = nullptr;
            return false;
        }
        return true;
    }

    void Stop()
    {
        if (!s_thread)
            return;

        SetEvent(s_stop);
        const DWORD r = WaitForSingleObject(s_thread, kStopWaitMs);
        if (r != WAIT_OBJECT_0)
            LOG("Watcher", "线程未在 %lu ms 内退出，句柄已放弃", kStopWaitMs);

        CloseHandle(s_thread);
        s_thread = nullptr;

        if (r == WAIT_OBJECT_0) {
            CloseHandle(s_stop);
            s_stop = nullptr;
            s_onChange = nullptr;
        }
    }
}

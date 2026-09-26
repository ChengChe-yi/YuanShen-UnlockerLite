#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "Game.h"

namespace
{

    static bool CheckRange(uintptr_t addr, size_t bytes, bool needWrite)
    {
        if (!addr || bytes == 0)
            return false;

        const uintptr_t end = addr + bytes;
        if (end < addr)
            return false;

        const DWORD readOk =
            PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
            PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
        const DWORD writeOk =
            PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
        const DWORD want = needWrite ? writeOk : readOk;

        MEMORY_BASIC_INFORMATION mbi{};
        uintptr_t cur = addr;

        while (cur < end) {
            if (VirtualQuery(reinterpret_cast<LPCVOID>(cur), &mbi, sizeof(mbi)) == 0)
                return false;
            if (mbi.State != MEM_COMMIT)
                return false;
            if (mbi.Protect & PAGE_GUARD)
                return false;
            if ((mbi.Protect & want) == 0)
                return false;

            const uintptr_t regionEnd = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
            if (regionEnd <= cur)
                return false;
            cur = regionEnd;
        }
        return true;
    }

    template <typename CharT>
    static bool ReadStringImpl(uintptr_t addr, CharT* out, size_t cch)
    {
        if (!addr || !out || cch < 1)
            return false;

        out[0] = CharT{};

        __try {
            const CharT* p = reinterpret_cast<const CharT*>(addr);

            size_t n = 0;
            while (n + 1 < cch && p[n] != CharT{})
                ++n;

            for (size_t i = 0; i < n; ++i)
                out[i] = p[i];
            out[n] = CharT{};
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            out[0] = CharT{};
            return false;
        }
    }
}

namespace Game
{
    uintptr_t ModuleBase(const wchar_t* module)
    {
        return reinterpret_cast<uintptr_t>(GetModuleHandleW(module));
    }

    uintptr_t Resolve(uintptr_t rva, const wchar_t* module)
    {
        const uintptr_t base = ModuleBase(module);
        return base ? base + rva : 0;
    }

    bool Query(uintptr_t addr, MEMORY_BASIC_INFORMATION& out)
    {
        if (!addr)
            return false;
        return VirtualQuery(reinterpret_cast<LPCVOID>(addr), &out, sizeof(out)) != 0;
    }

    bool IsReadable(uintptr_t addr, size_t bytes) { return CheckRange(addr, bytes, false); }
    bool IsWritable(uintptr_t addr, size_t bytes) { return CheckRange(addr, bytes, true); }

    bool ReadUtf8(uintptr_t addr, char* out, size_t cch)
    {
        return ReadStringImpl<char>(addr, out, cch);
    }

    bool ReadUtf16(uintptr_t addr, wchar_t* out, size_t cch)
    {
        return ReadStringImpl<wchar_t>(addr, out, cch);
    }
}

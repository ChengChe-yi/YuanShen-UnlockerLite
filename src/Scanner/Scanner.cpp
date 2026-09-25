#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "Scanner.h"
#include "Game.h"
#include <cstring>

namespace
{
    constexpr size_t kMaxPatternBytes = 256;

    struct Pattern
    {
        uint8_t bytes[kMaxPatternBytes] = {};
        bool    wildcard[kMaxPatternBytes] = {};
        size_t  length = 0;
        size_t  firstFixed = 0;      
        bool    hasFixed = false;
    };

    int HexVal(char c)
    {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }


    bool ParsePattern(const char* signature, Pattern& out)
    {
        if (!signature)
            return false;

        const char* p = signature;
        while (*p) {
            while (*p == ' ' || *p == '\t')
                ++p;
            if (!*p)
                break;

            if (out.length >= kMaxPatternBytes)
                return false;

            if (*p == '?') {
                out.wildcard[out.length] = true;
                out.bytes[out.length] = 0;
                ++p;
                if (*p == '?')          
                    ++p;
            }
            else {
                const int hi = HexVal(p[0]);
                const int lo = p[1] ? HexVal(p[1]) : -1;
                if (hi < 0 || lo < 0)
                    return false;

                out.wildcard[out.length] = false;
                out.bytes[out.length] = static_cast<uint8_t>((hi << 4) | lo);
                p += 2;

                if (!out.hasFixed) {
                    out.hasFixed = true;
                    out.firstFixed = out.length;
                }
            }
            ++out.length;
        }
        return out.length > 0;
    }


    uintptr_t ScanTyped(const uint8_t* begin, size_t size, const Pattern& pat)
    {
        if (!begin || !pat.hasFixed || size < pat.length)
            return 0;

        const size_t last = size - pat.length;      

        for (size_t i = 0; i <= last; ) {

            const uint8_t* slot = begin + i + pat.firstFixed;
            const void* hit = memchr(slot, pat.bytes[pat.firstFixed], last - i + 1);
            if (!hit)
                return 0;

            i = static_cast<const uint8_t*>(hit) - begin - pat.firstFixed;

            bool ok = true;
            for (size_t k = 0; k < pat.length; ++k) {
                if (!pat.wildcard[k] && begin[i + k] != pat.bytes[k]) {
                    ok = false;
                    break;
                }
            }
            if (ok)
                return reinterpret_cast<uintptr_t>(begin + i);

            ++i;                                    
        }
        return 0;
    }
}

namespace Scanner
{
    uintptr_t ScanRange(uintptr_t start, size_t size, const char* signature)
    {
        Pattern pat;
        if (!start || size == 0 || !ParsePattern(signature, pat))
            return 0;


        __try {
            return ScanTyped(reinterpret_cast<const uint8_t*>(start), size, pat);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return 0;
        }
    }

    uintptr_t ScanModule(const wchar_t* module, const char* signature)
    {
        const uintptr_t base = Game::ModuleBase(module);
        if (!base)
            return 0;

        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE)
            return 0;

        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE)
            return 0;

        const WORD sectionCount = nt->FileHeader.NumberOfSections;
        const auto* section = IMAGE_FIRST_SECTION(nt);

        for (WORD i = 0; i < sectionCount; ++i) {
            const size_t size = section[i].Misc.VirtualSize;
            if (section[i].VirtualAddress == 0 || size == 0)
                continue;
            if ((section[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0)
                continue;

            const uintptr_t addr = base + section[i].VirtualAddress;
            if (!Game::IsReadable(addr, size))      
                continue;

            if (const uintptr_t hit = ScanRange(addr, size, signature))
                return hit;
        }
        return 0;
    }

    uintptr_t ScanMainMod(const char* signature)
    {
        return ScanModule(nullptr, signature);
    }

    uintptr_t ResolveRelative(uintptr_t instruction, int offset, int instrSize)
    {
        if (!instruction)
            return 0;

        const uintptr_t field = instruction + offset;
        if (!Game::IsReadable(field, sizeof(int32_t)))
            return 0;

        const int32_t rel = *reinterpret_cast<const int32_t*>(field);
        return instruction + instrSize + static_cast<intptr_t>(rel);
    }

    uintptr_t ScanCallTarget(const char* signature, int relOffset, int instrSize)
    {
        return ResolveRelative(ScanMainMod(signature), relOffset, instrSize);
    }
}

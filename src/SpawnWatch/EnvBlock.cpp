#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "EnvBlock.h"
#include <cwchar>


namespace EnvBlock
{
  
    static constexpr size_t kMaxBlockChars = 1u << 18;   


    static size_t BoundedLen(const wchar_t* p, const wchar_t* limit)
    {
        const wchar_t* q = p;
        while (q < limit && *q)
            ++q;
        return static_cast<size_t>(q - p);
    }


    static size_t BlockChars(const wchar_t* block)
    {
        const wchar_t* const limit = block + kMaxBlockChars;
        const wchar_t* p = block;

        for (;;) {
            if (p >= limit)
                return 0;                                  
            if (*p == 0)
                return static_cast<size_t>(p - block) + 1;  

            const size_t len = BoundedLen(p, limit);
            if (p + len >= limit)
                return 0;                                 
            p += len + 1;
        }
    }


    static bool SameName(const wchar_t* entry, size_t entryLen,
                         const wchar_t* name, size_t nameLen)
    {
        if (entryLen < nameLen + 1)          
            return false;

        for (size_t i = 0; i < nameLen; ++i) {
            wchar_t a = entry[i];
            wchar_t b = name[i];
            if (a >= L'A' && a <= L'Z') a = static_cast<wchar_t>(a + 32);
            if (b >= L'A' && b <= L'Z') b = static_cast<wchar_t>(b + 32);
            if (a != b)
                return false;
        }
        return entry[nameLen] == L'=';
    }

    static wchar_t* WriteEntry(wchar_t* dst, const wchar_t* name, size_t nameLen,
                               const wchar_t* value, size_t valueLen)
    {
        wmemcpy(dst, name, nameLen);
        dst += nameLen;
        *dst++ = L'=';
        if (valueLen) {
            wmemcpy(dst, value, valueLen);
            dst += valueLen;
        }
        *dst++ = L'\0';
        return dst;
    }

    wchar_t* AddOrReplace(const wchar_t* src, const wchar_t* name, const wchar_t* value)
    {
        if (!src || !name || !*name)
            return nullptr;

        const size_t srcChars = BlockChars(src);
        if (srcChars == 0)
            return nullptr;

        const size_t nameLen  = wcslen(name);
        const size_t valueLen = value ? wcslen(value) : 0;
        const size_t entryLen = nameLen + 1 + valueLen;


        const wchar_t* const dataEnd = src + srcChars - 1;


        size_t total = 0;
        bool replaced = false;
        for (const wchar_t* p = src; p < dataEnd; ) {
            const size_t len = BoundedLen(p, dataEnd);
            if (SameName(p, len, name, nameLen)) {
                if (!replaced) {
                    total += entryLen + 1;
                    replaced = true;
                }
            } else {
                total += len + 1;
            }
            p += len + 1;
        }
        if (!replaced)
            total += entryLen + 1;
        ++total;                                          

        wchar_t* out = static_cast<wchar_t*>(
            HeapAlloc(GetProcessHeap(), 0, total * sizeof(wchar_t)));
        if (!out)
            return nullptr;


        wchar_t* dst = out;
        replaced = false;
        for (const wchar_t* p = src; p < dataEnd; ) {
            const size_t len = BoundedLen(p, dataEnd);
            if (SameName(p, len, name, nameLen)) {
                if (!replaced) {
                    dst = WriteEntry(dst, name, nameLen, value, valueLen);
                    replaced = true;
                }
            } else {
                wmemcpy(dst, p, len);
                dst += len;
                *dst++ = L'\0';
            }
            p += len + 1;
        }
        if (!replaced)
            dst = WriteEntry(dst, name, nameLen, value, valueLen);

        *dst = L'\0';
        return out;
    }

    void Free(wchar_t* block)
    {
        if (block)
            HeapFree(GetProcessHeap(), 0, block);
    }
}

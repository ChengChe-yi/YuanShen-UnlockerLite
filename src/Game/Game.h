#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace Game
{

    uintptr_t ModuleBase(const wchar_t* module = nullptr);

    uintptr_t Resolve(uintptr_t rva, const wchar_t* module = nullptr);

    bool Query(uintptr_t addr, MEMORY_BASIC_INFORMATION& out);

    bool IsReadable(uintptr_t addr, size_t bytes = 1);
    bool IsWritable(uintptr_t addr, size_t bytes = 1);

    template <typename T>
    bool TryRead(uintptr_t addr, T& out);

    template <typename T>
    T Read(uintptr_t addr, T fallback = T{});

    bool ReadUtf8(uintptr_t addr, char* out, size_t cch);
    bool ReadUtf16(uintptr_t addr, wchar_t* out, size_t cch);

    template <typename T>
    bool TryWrite(uintptr_t addr, const T& v);

    template <typename T>
    volatile T* AcquireWritable(uintptr_t addr);

    template <typename T>
    bool TryRead(uintptr_t addr, T& out)
    {
        static_assert(std::is_trivially_copyable_v<T>,
                      "TryRead 只支持可平凡复制的类型");
        if (!addr)
            return false;

        __try {
            out = *reinterpret_cast<const T*>(addr);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    template <typename T>
    T Read(uintptr_t addr, T fallback)
    {
        T v{};
        return TryRead(addr, v) ? v : fallback;
    }

    template <typename T>
    bool TryWrite(uintptr_t addr, const T& v)
    {
        static_assert(std::is_trivially_copyable_v<T>,
                      "TryWrite 只支持可平凡复制的类型");
        if (!addr)
            return false;

        __try {

            *reinterpret_cast<volatile T*>(addr) = v;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    template <typename T>
    volatile T* AcquireWritable(uintptr_t addr)
    {
        static_assert(std::is_trivially_copyable_v<T>, "AcquireWritable 只支持可平凡复制的类型");
        return IsWritable(addr, sizeof(T)) ? reinterpret_cast<volatile T*>(addr) : nullptr;
    }
}

#pragma once
#include <cstdint>

namespace Patterns
{
    namespace Rva
    {
        inline constexpr uintptr_t FpsLimit = 0x54CD60C;

       
        inline constexpr uintptr_t TargetFrameRateGetter = 0x145A4D0;
    }

    namespace Sig
    {
        inline constexpr char FpsReadInFramePacer[] =
            "66 0F 6E 0D ?? ?? ?? ?? 0F 57 C0 0F 5B C9";

        inline constexpr char TargetFrameRateGetterCall[] =
            "E8 ? ? ? ? 85 C0 7E 0E E8 ? ? ? ? 0F 57 C0 F3 0F 2A C0 EB 08";

        inline constexpr char CameraSetFieldOfView[] =
            "40 53 48 83 EC 60 0F 29 74 24 ? 48 8B D9 0F 28 F1 E8 ? ? ? ? 48 85 C0 0F 84 ? ? ? ? E8 ? ? ? ? 48 8B C8";
    }
}

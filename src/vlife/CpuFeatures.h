//
// CPU feature detection for runtime dispatch
//

#pragma once

#include <atomic>

#ifdef VLIFE_AVX512_ENABLED

namespace CpuFeatures {

// Runtime detection of AVX-512 support
// Returns true if the CPU supports AVX-512F and AVX-512BW (required for byte operations)
inline bool detectAVX512Support() {
#if defined(VLIFE_AVX512_NATIVE) && (defined(__x86_64__) || defined(_M_X64))
    // Native x86_64 build: check CPUID for actual hardware support
    // We need AVX-512F (foundation) and AVX-512BW (byte/word operations)

    #if defined(__GNUC__) || defined(__clang__)
        unsigned int eax, ebx, ecx, edx;

        // Check if CPUID leaf 7 is supported
        __asm__ __volatile__(
            "cpuid"
            : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
            : "a"(0)
        );
        if (eax < 7) return false;

        // CPUID leaf 7, subleaf 0: extended features
        __asm__ __volatile__(
            "cpuid"
            : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
            : "a"(7), "c"(0)
        );

        // Check EBX bits:
        // Bit 16: AVX-512F (foundation)
        // Bit 30: AVX-512BW (byte/word)
        bool hasAVX512F = (ebx >> 16) & 1;
        bool hasAVX512BW = (ebx >> 30) & 1;

        return hasAVX512F && hasAVX512BW;
    #elif defined(_MSC_VER)
        int cpuInfo[4];
        __cpuid(cpuInfo, 0);
        if (cpuInfo[0] < 7) return false;

        __cpuidex(cpuInfo, 7, 0);
        bool hasAVX512F = (cpuInfo[1] >> 16) & 1;
        bool hasAVX512BW = (cpuInfo[1] >> 30) & 1;

        return hasAVX512F && hasAVX512BW;
    #else
        return false;
    #endif
#else
    // SIMDE emulation mode: always return true since SIMDE handles it
    return true;
#endif
}

// Cached result for runtime checks (initialized on first use)
inline bool hasAVX512Support() {
    static const bool supported = detectAVX512Support();
    return supported;
}

} // namespace CpuFeatures

#endif // VLIFE_AVX512_ENABLED

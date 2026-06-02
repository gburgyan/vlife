//
// AppleSiliconCounters.h - Hardware Performance Counter Access for Apple Silicon
//
// Provides direct access to CPU performance counters on Apple Silicon (M1/M2/M3)
// using the private kperf framework. Requires sudo or SIP disabled to run.
//
// When ENABLE_KPERF_COUNTERS is defined, provides:
//   - Cycle counting (FIXED_CYCLES)
//   - Instruction counting (FIXED_INSTRUCTIONS)
//   - L1D cache miss counting
//   - Branch misprediction counting
//
// When not defined, all operations are no-ops with zero overhead.
//
// Based on:
//   - ibireme's M1 counter access gist: https://gist.github.com/ibireme/173517c208c7dc333ba962c1f0d67d12
//   - simple-kpc: https://github.com/lunacookies/simple-kpc
//   - perf-macos: https://github.com/DominikHorn/perf-macos
//

#pragma once

#include <cstdint>

#ifdef __APPLE__

// Hardware counter data structure
struct HardwareCounters {
    uint64_t cycles = 0;
    uint64_t instructions = 0;
    uint64_t l1d_cache_misses = 0;
    uint64_t branch_mispredictions = 0;

    // Derived metrics
    double ipc() const {
        return cycles > 0 ? static_cast<double>(instructions) / cycles : 0.0;
    }

    double l1d_miss_rate() const {
        // Misses per 1000 instructions
        return instructions > 0 ? (static_cast<double>(l1d_cache_misses) * 1000.0) / instructions : 0.0;
    }

    double branch_mispred_rate() const {
        // Mispredictions per 1000 instructions
        return instructions > 0 ? (static_cast<double>(branch_mispredictions) * 1000.0) / instructions : 0.0;
    }

    // Arithmetic operators for computing deltas
    HardwareCounters operator-(const HardwareCounters& other) const {
        return {
            cycles - other.cycles,
            instructions - other.instructions,
            l1d_cache_misses - other.l1d_cache_misses,
            branch_mispredictions - other.branch_mispredictions
        };
    }

    HardwareCounters& operator+=(const HardwareCounters& other) {
        cycles += other.cycles;
        instructions += other.instructions;
        l1d_cache_misses += other.l1d_cache_misses;
        branch_mispredictions += other.branch_mispredictions;
        return *this;
    }
};

#ifdef ENABLE_KPERF_COUNTERS

// CounterSession - RAII wrapper for hardware counter access
class CounterSession {
public:
    // Initialize kperf framework and configure counters
    // Throws std::runtime_error if initialization fails
    CounterSession();

    // Clean up kperf state
    ~CounterSession();

    // Start counting (resets counters)
    void start();

    // Read current counter values (does not stop counting)
    HardwareCounters read();

    // Stop counting and return delta since start()
    HardwareCounters stop();

    // Check if running on Apple Silicon with kperf access
    static bool isSupported();

    // Get error message if initialization failed
    const char* getError() const { return errorMessage; }

    // Check if session is valid
    bool isValid() const { return initialized; }

private:
    bool initialized = false;
    bool counting = false;
    const char* errorMessage = nullptr;

    // Counter values at start()
    uint64_t startCycles = 0;
    uint64_t startInstructions = 0;
    uint64_t startL1dMisses = 0;
    uint64_t startBranchMispred = 0;

    // Read raw counter values
    bool readCounters(uint64_t& cycles, uint64_t& instructions,
                      uint64_t& l1dMisses, uint64_t& branchMispred);
};

#else // !ENABLE_KPERF_COUNTERS

// Stub implementation when kperf is disabled
class CounterSession {
public:
    CounterSession() = default;
    ~CounterSession() = default;

    void start() {}
    HardwareCounters read() { return {}; }
    HardwareCounters stop() { return {}; }

    static bool isSupported() { return false; }
    const char* getError() const { return "ENABLE_KPERF_COUNTERS not defined"; }
    bool isValid() const { return false; }
};

#endif // ENABLE_KPERF_COUNTERS

#else // !__APPLE__

// Non-Apple stub
struct HardwareCounters {
    uint64_t cycles = 0;
    uint64_t instructions = 0;
    uint64_t l1d_cache_misses = 0;
    uint64_t branch_mispredictions = 0;

    double ipc() const { return 0.0; }
    double l1d_miss_rate() const { return 0.0; }
    double branch_mispred_rate() const { return 0.0; }

    HardwareCounters operator-(const HardwareCounters& other) const { return {}; }
    HardwareCounters& operator+=(const HardwareCounters& other) { return *this; }
};

class CounterSession {
public:
    void start() {}
    HardwareCounters read() { return {}; }
    HardwareCounters stop() { return {}; }
    static bool isSupported() { return false; }
    const char* getError() const { return "Not running on Apple platform"; }
    bool isValid() const { return false; }
};

#endif // __APPLE__

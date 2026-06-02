//
// AppleSiliconCounters.cpp - Implementation of hardware counter access
//
// Uses dlopen to load Apple's private kperf framework and access
// performance monitoring counters on Apple Silicon CPUs.
//
// Based on ibireme's gist: https://gist.github.com/ibireme/173517c208c7dc333ba962c1f0d67d12
//
// Requires sudo to run (or SIP disabled) for kperf access.
//

#include "AppleSiliconCounters.h"

#ifdef __APPLE__
#ifdef ENABLE_KPERF_COUNTERS

#include <dlfcn.h>
#include <cstdio>
#include <stdexcept>
#include <pthread.h>

// ============================================================================
// kperf/kpc framework definitions
// ============================================================================

// KPC class masks
#define KPC_CLASS_FIXED_MASK        (1 << 0)  // Fixed counters (cycles, instructions)
#define KPC_CLASS_CONFIGURABLE_MASK (1 << 1)  // Programmable counters

// Number of counters
#define KPC_MAX_COUNTERS 10  // 2 fixed + 8 configurable on Apple Silicon

// Apple Silicon performance events (from PMU documentation)
// These are the event selectors for M1/M2/M3 chips
// Format: 0xEECC where EE = event, CC = counter mask

// Core PMU events (compatible across M1/M2/M3)
#define CPMU_CORE_CYCLE             0x02  // CPU cycles
#define CPMU_INST_RETIRED           0x8c  // Instructions retired

// Memory events
#define CPMU_L1D_CACHE_MISS_LD      0xa3  // L1D cache load miss
#define CPMU_L1D_CACHE_MISS_ST      0xa4  // L1D cache store miss
#define CPMU_L1D_TLB_MISS           0xa6  // L1D TLB miss

// Branch events
#define CPMU_BRANCH_MISPRED         0xcb  // Branch misprediction

// Instruction fetch
#define CPMU_INST_FETCH_RESTART     0x8d  // Instruction fetch restart

// ============================================================================
// Function pointer types for kperf/kpc
// ============================================================================

typedef int (*kpc_get_counter_count_func)(uint32_t classes);
typedef int (*kpc_get_config_count_func)(uint32_t classes);
typedef int (*kpc_set_counting_func)(uint32_t classes);
typedef int (*kpc_get_counting_func)(void);
typedef int (*kpc_set_thread_counting_func)(uint32_t classes);
typedef int (*kpc_get_thread_counting_func)(void);
typedef int (*kpc_set_config_func)(uint32_t classes, uint64_t* config);
typedef int (*kpc_get_config_func)(uint32_t classes, uint64_t* config);
typedef int (*kpc_get_thread_counters_func)(int tid, uint32_t buf_count, uint64_t* buf);
typedef int (*kpc_force_all_ctrs_set_func)(int val);
typedef int (*kpc_get_force_all_ctrs_func)(void);
typedef int (*kperf_sample_get_func)(void* buf);

// ============================================================================
// Global state for kperf framework
// ============================================================================

static bool g_kperf_loaded = false;
static bool g_kperf_initialized = false;
static const char* g_init_error = nullptr;

// Function pointers
static kpc_get_counter_count_func kpc_get_counter_count = nullptr;
static kpc_get_config_count_func kpc_get_config_count = nullptr;
static kpc_set_counting_func kpc_set_counting = nullptr;
static kpc_get_counting_func kpc_get_counting = nullptr;
static kpc_set_thread_counting_func kpc_set_thread_counting = nullptr;
static kpc_get_thread_counting_func kpc_get_thread_counting = nullptr;
static kpc_set_config_func kpc_set_config = nullptr;
static kpc_get_config_func kpc_get_config = nullptr;
static kpc_get_thread_counters_func kpc_get_thread_counters = nullptr;
static kpc_force_all_ctrs_set_func kpc_force_all_ctrs_set = nullptr;
static kpc_get_force_all_ctrs_func kpc_get_force_all_ctrs = nullptr;

// Counter configuration
static int g_counter_count = 0;
static int g_config_count = 0;
static uint64_t g_config[KPC_MAX_COUNTERS] = {0};

// ============================================================================
// Framework loading
// ============================================================================

static bool loadKperfFramework() {
    if (g_kperf_loaded) return true;

    // Load kperf.framework
    void* kperf = dlopen(
        "/System/Library/PrivateFrameworks/kperf.framework/kperf",
        RTLD_NOW | RTLD_LOCAL
    );
    if (!kperf) {
        g_init_error = "Failed to load kperf.framework - may need sudo or SIP disabled";
        return false;
    }

    // Load function pointers
    #define LOAD_SYMBOL(name) \
        name = (name##_func)dlsym(kperf, #name); \
        if (!name) { g_init_error = "Failed to load symbol: " #name; return false; }

    LOAD_SYMBOL(kpc_get_counter_count);
    LOAD_SYMBOL(kpc_get_config_count);
    LOAD_SYMBOL(kpc_set_counting);
    LOAD_SYMBOL(kpc_get_counting);
    LOAD_SYMBOL(kpc_set_thread_counting);
    LOAD_SYMBOL(kpc_get_thread_counting);
    LOAD_SYMBOL(kpc_set_config);
    LOAD_SYMBOL(kpc_get_config);
    LOAD_SYMBOL(kpc_get_thread_counters);
    LOAD_SYMBOL(kpc_force_all_ctrs_set);
    LOAD_SYMBOL(kpc_get_force_all_ctrs);

    #undef LOAD_SYMBOL

    g_kperf_loaded = true;
    return true;
}

// ============================================================================
// Counter initialization
// ============================================================================

static bool initializeCounters() {
    if (g_kperf_initialized) return true;
    if (!loadKperfFramework()) return false;

    // Get counter counts
    uint32_t classes = KPC_CLASS_FIXED_MASK | KPC_CLASS_CONFIGURABLE_MASK;
    g_counter_count = kpc_get_counter_count(classes);
    g_config_count = kpc_get_config_count(classes);

    if (g_counter_count == 0 || g_config_count == 0) {
        g_init_error = "No performance counters available";
        return false;
    }

    // Force all counters to be available (requires sudo)
    int ret = kpc_force_all_ctrs_set(1);
    if (ret != 0) {
        g_init_error = "kpc_force_all_ctrs_set failed - requires sudo";
        return false;
    }

    // Configure counters
    // Fixed counters (0, 1): cycles and instructions - always available
    // Configurable counters: we program for L1D misses and branch mispredictions

    // The config array layout depends on the chip, but typically:
    // config[0], config[1] = fixed counter config (often unused/zeroed)
    // config[2], config[3], ... = configurable counter events

    // Clear config
    for (int i = 0; i < KPC_MAX_COUNTERS; i++) {
        g_config[i] = 0;
    }

    // Configure programmable counters for the events we want
    // On Apple Silicon, config values are typically just the event number
    // Index 2 = first configurable counter
    if (g_config_count > 2) {
        g_config[2] = CPMU_L1D_CACHE_MISS_LD;  // L1D load misses
    }
    if (g_config_count > 3) {
        g_config[3] = CPMU_BRANCH_MISPRED;      // Branch mispredictions
    }

    ret = kpc_set_config(classes, g_config);
    if (ret != 0) {
        g_init_error = "kpc_set_config failed";
        return false;
    }

    // Enable counting
    ret = kpc_set_counting(classes);
    if (ret != 0) {
        g_init_error = "kpc_set_counting failed";
        return false;
    }

    ret = kpc_set_thread_counting(classes);
    if (ret != 0) {
        g_init_error = "kpc_set_thread_counting failed";
        return false;
    }

    g_kperf_initialized = true;
    return true;
}

// ============================================================================
// CounterSession implementation
// ============================================================================

CounterSession::CounterSession() {
    if (!initializeCounters()) {
        errorMessage = g_init_error;
        initialized = false;
        return;
    }
    initialized = true;
}

CounterSession::~CounterSession() {
    // Note: We don't clean up global kperf state here since other sessions
    // may be using it. The counters will be released when the process exits.
}

bool CounterSession::isSupported() {
    // Try to load the framework to check if it's available
    // This doesn't require sudo - just checks if the framework exists
    void* kperf = dlopen(
        "/System/Library/PrivateFrameworks/kperf.framework/kperf",
        RTLD_NOW | RTLD_LOCAL
    );
    if (kperf) {
        dlclose(kperf);
        return true;
    }
    return false;
}

bool CounterSession::readCounters(uint64_t& cycles, uint64_t& instructions,
                                   uint64_t& l1dMisses, uint64_t& branchMispred) {
    if (!initialized) return false;

    uint64_t counters[KPC_MAX_COUNTERS] = {0};
    int ret = kpc_get_thread_counters(0, g_counter_count, counters);
    if (ret != 0) {
        return false;
    }

    // On Apple Silicon:
    // counters[0] = fixed cycles
    // counters[1] = fixed instructions
    // counters[2] = first configurable (L1D misses)
    // counters[3] = second configurable (branch mispred)

    cycles = counters[0];
    instructions = counters[1];
    l1dMisses = (g_counter_count > 2) ? counters[2] : 0;
    branchMispred = (g_counter_count > 3) ? counters[3] : 0;

    return true;
}

void CounterSession::start() {
    if (!initialized) return;

    // Read current counter values as baseline
    readCounters(startCycles, startInstructions, startL1dMisses, startBranchMispred);
    counting = true;
}

HardwareCounters CounterSession::read() {
    if (!initialized || !counting) return {};

    uint64_t cycles, instructions, l1dMisses, branchMispred;
    if (!readCounters(cycles, instructions, l1dMisses, branchMispred)) {
        return {};
    }

    return {
        cycles - startCycles,
        instructions - startInstructions,
        l1dMisses - startL1dMisses,
        branchMispred - startBranchMispred
    };
}

HardwareCounters CounterSession::stop() {
    if (!initialized || !counting) return {};

    HardwareCounters result = read();
    counting = false;
    return result;
}

#endif // ENABLE_KPERF_COUNTERS
#endif // __APPLE__

# VLife Performance Optimization Analysis

This document analyzes the performance optimizations implemented in VLife, a high-performance Conway's Game of Life simulation. The implementation uses a tiled architecture with sophisticated optimization techniques targeting cache efficiency, branch prediction, SIMD vectorization, and parallel execution.

**Related Documentation:**
- [PROFILING.md](PROFILING.md) - How to profile VLife with sample and Instruments
- [METRICS.md](METRICS.md) - Activity metrics collection (k, N, timing breakdown)

---

## Table of Contents

1. [Data Structure Layout Optimizations](#1-data-structure-layout-optimizations)
2. [Cache Optimization Strategies](#2-cache-optimization-strategies)
3. [Lookup Table (LUT) Design](#3-lookup-table-lut-design)
4. [Branch Prediction Optimizations](#4-branch-prediction-optimizations)
5. [SIMD Vectorization](#5-simd-vectorization)
6. [Parallel Execution & Lock-Free Patterns](#6-parallel-execution--lock-free-patterns)
7. [Algorithmic Optimizations](#7-algorithmic-optimizations)
8. [Optimization Interdependencies](#8-optimization-interdependencies)
9. [Code Path Specific Optimizations](#9-code-path-specific-optimizations)
10. [Quantitative Analysis](#10-quantitative-analysis)
11. [Apple Silicon Hardware Counter Profiling](#11-apple-silicon-hardware-counter-profiling)

---

## 1. Data Structure Layout Optimizations

### 1.1 Tile Class Cache-Line Alignment

**Location:** `Tile.h:15-62`

```cpp
class alignas(64) Tile {
    // === Hot path metadata (first cache line, 64 bytes) ===
    uint64_t wasModified{~0ULL};         // 8 bytes - block modification tracking
    uint32_t rowChangeMask{0};           // 4 bytes - Phase 2 row skip
    uint32_t liveCount;                  // 4 bytes - live cell count
    uint8_t activityRows{0};             // 1 byte  - eviction tracking
    uint8_t lastModifiedGeneration{0};   // 1 byte  - timestamp
    std::atomic<uint8_t> queuedForPhase1{0}; // 1 byte - queue flag
    // 5 bytes implicit padding

    // === Cell data (8 cache lines, 512 bytes) ===
    uint64_t cells[64]{};

    // === Change tracking (2 cache lines, 128 bytes) ===
    uint32_t changes[32]{};

    // === Cold metadata (infrequent access) ===
    VLife *board;
    int32_t tileX, tileY;
    Tile *left, *right, *up, *down;
};
```

**Cache Cost Analysis:**

| Field Group | Size | Cache Lines | Access Pattern |
|-------------|------|-------------|----------------|
| Hot metadata | 24 bytes (+padding) | 1 | Every Phase 1/2 |
| Cell data | 512 bytes | 8 | Phase 1 scan |
| Changes | 128 bytes | 2 | Phase 2 apply |
| Cold metadata | ~56 bytes | 1 | Tile creation/linking |

**Expected Benefit:**
- **False sharing prevention:** 64-byte alignment ensures tiles don't share cache lines
- **Hot path locality:** Phase 1 checks (wasModified, rowChangeMask) fit in first cache line
- **Prefetch efficiency:** Contiguous cell data enables hardware prefetching

### 1.2 Cell Nibble Encoding

**Location:** `Tile.cpp:42-49`, `VLife.cpp:477-554`

Each cell uses 4 bits (nibble):
```
Bit 3:   ALIVE (1 = live, 0 = dead)
Bits 2-0: NEIGHBOR_COUNT (0-7, excluding paired cell)
```

Two cells packed per byte:
```
Byte: [LEFT_ALIVE | LEFT_COUNT | RIGHT_ALIVE | RIGHT_COUNT]
      [   bit 7   | bits 6-4   |    bit 3    | bits 2-0   ]
```

**Memory Savings:**
- Naive: 8 neighbors x 1 byte + 1 byte state = 9 bytes/cell
- VLife: 0.5 bytes/cell (4 bits)
- **Compression ratio: 18:1**

**Implicit Neighbor Optimization:**
```cpp
// From VLife.cpp:507-508
leftNeighbors += rightAlive;   // Paired cell is implicit neighbor
rightNeighbors += leftAlive;
```

---

## 2. Cache Optimization Strategies

### 2.1 Prefetching

**Location:** `Tile.cpp:9-19`, `Tile.cpp:264-266`, `Tile.cpp:433-435`

```cpp
// Cross-platform prefetch abstraction
#if defined(__x86_64__)
    #define PREFETCH(addr) _mm_prefetch((const char*)(addr), _MM_HINT_T0)
#elif defined(__aarch64__)
    #define PREFETCH(addr) __builtin_prefetch((addr), 0, 3)
#endif
```

**Prefetch Locations:**

| Location | Distance | Purpose |
|----------|----------|---------|
| Phase 1 NEON (line 264) | 8 words ahead | 2 iterations ahead |
| Phase 1 scalar (line 433) | 4 words ahead | Next iteration |
| Phase 2 (line 874-876) | 2-3 words | Current row cells |

**Expected Latency Hiding:**
- L1 cache miss: ~4 cycles
- L2 cache miss: ~12 cycles
- Prefetch distance of 4-8 words (~256-512 bytes) hides L2 latency at typical iteration rates

### 2.2 Hierarchical Skip Optimization

**Location:** `Tile.cpp:437-441`

```cpp
// Quick zero-check of 4 words (256 cells) at once
uint64_t combined = cells[i] | cells[i+1] | cells[i+2] | cells[i+3];
if (combined == 0) continue;  // Skip dead regions entirely
```

**Cache Access Pattern:**
- Without skip: 64 x 8 = 512 byte cache loads per tile
- With skip (sparse pattern): ~50-100 byte loads (80-90% reduction)

### 2.3 Block-Row Granularity Processing

**Location:** `Tile.cpp:244-252`

Processing at 8-word (512-byte) block row granularity aligns with:
- L1 cache line size (64 bytes x 8 = 512 bytes)
- Hardware prefetcher stride detection
- Modification mask word boundaries

---

## 3. Lookup Table (LUT) Design

### 3.1 LUT Summary

| LUT | Size | Entries | Index Encoding | Output |
|-----|------|---------|----------------|--------|
| `ruleLUT` | 256 bytes | 256 | `[leftAlive:1][leftCount:3][rightAlive:1][rightCount:3]` | 2-bit change flags |
| `deltaLUT` | 96 bytes | 16 | `[leftState:2][rightState:2]` | 6 int8_t deltas |
| `toggleMaskLUT` | 256 bytes | 32 | `[pairInWord:3][changeBits:2]` | 64-bit XOR mask |
| `stateDeltaLUT` | 64 bytes | 16 | `[changeBits:2][aliveState:2]` | Combined state/delta |
| `compactCornerMaskLUT` | 512 bytes | 256 | `[yClass:2][xPair:4][changeState:2]` | 2 x uint8_t masks |

**Total LUT Footprint: 1,184 bytes** (fits entirely in L1 data cache)

### 3.2 Rule LUT Details

**Location:** `VLife.cpp:477-554`

```cpp
// Index: 8 bits encoding two cells
// Bits 7: left alive, Bits 6-4: left neighbors, Bit 3: right alive, Bits 2-0: right neighbors

int result = leftChanged << 1 | rightChanged;
ruleLUT[i] = static_cast<std::byte>(result);
```

**Branch Elimination:**
- Without LUT: ~8-10 conditional branches per cell pair
- With LUT: 1 memory load, 0 branches

### 3.3 Compact Corner Mask LUT

**Location:** `VLife.cpp:638-688`

**Size Reduction via Y-Symmetry:**
- Naive approach: 32 x 32 x 4 = 4096 entries x 2 bytes = 8KB
- Compact approach: 4 x 16 x 4 = 256 entries x 2 bytes = 512 bytes
- **Reduction: 16x smaller**

```cpp
// Y-symmetry exploitation: upper and lower corners have same x-pattern
// Runtime shifts mask to correct block-row position
int upperRow = blockRow - (yClass == 0);
int lowerRow = blockRow + (yClass == 3);
```

---

## 4. Branch Prediction Optimizations

### 4.1 Branchless Bit Manipulation

**Location:** `Tile.cpp:863-892`

```cpp
// Branchless lowest-bit clearing
remainingRows &= remainingRows - 1;

// Branchless highest-bit finding
int leadingZeros = __builtin_clz(changeBits);
changeBits &= ~(0x3U << shiftAmount);  // Clear without branch
```

**Branch Misprediction Cost:**
- Modern CPUs: ~15-20 cycles per misprediction
- Inner loop processes ~10-100 cell pairs per tile
- Branchless code eliminates ~3-5 potential mispredictions per cell pair

### 4.2 Branchless Delta Accumulation

**Location:** `Tile.cpp:790-793`, `Tile.cpp:1218-1235`

```cpp
// Comment at line 790: "Branchless - adding 0 is a no-op, avoids branch misprediction"
deltaArray[x] += delta;  // No delta==0 check

// Bulk 4-nibble update without branches
int c0 = ((word & m0) >> baseBitPos) + deltas[0];
int c1 = ((word & m1) >> (baseBitPos + 4)) + deltas[1];
int c2 = ((word & m2) >> (baseBitPos + 8)) + deltas[2];
int c3 = ((word & m3) >> (baseBitPos + 12)) + deltas[3];
```

### 4.3 Branch Prediction Hints

**Location:** `VLife.h:248`

```cpp
if (__builtin_expect((generationNumber & (EVICTION_CHECK_INTERVAL - 1)) == 0, 0)) {
    evictDeadTilesActual();  // Cold path, hint: unlikely
}
```

### 4.4 State Extraction via Bit Manipulation

**Location:** `Tile.cpp:929-931`

```cpp
// Extract alive state without branches
int aliveState = ((cellByte >> 6) & 2) | ((cellByte >> 3) & 1);
// Produces: (leftWasAlive << 1) | rightWasAlive in 2 shifts + 2 ANDs
```

---

## 5. SIMD Vectorization

### 5.1 AVX-512 Implementation

**Location:** `Tile.cpp:576-785`

**Enabled via:** `cmake -DENABLE_AVX512=ON`

```cpp
// Process 64 bytes (64 cell pairs) per iteration
__m512i data = _mm512_loadu_si512(...);

// Mask-based alive bit extraction
__mmask64 left_alive_mask = _mm512_test_epi8_mask(data, mask_left_alive);
__mmask64 right_alive_mask = _mm512_test_epi8_mask(data, mask_right_alive);
```

**Performance Characteristics:**
- Processes 64 cell pairs per iteration vs 8 in scalar
- Eliminates LUT lookups entirely (direct computation)
- **Expected Phase 1 speedup: 2-4x**

**Limitations:**
- AVX-512 can cause frequency throttling on some Intel CPUs
- Requires Ice Lake+ (Intel) or Zen 4+ (AMD) for native support
- SIMDE fallback available for portability

### 5.2 ARM NEON Optimizations

**Location:** `Tile.cpp:239-410`

```cpp
#ifdef USE_NEON_SIMD
// Optimized byte-level LUT lookups with packed results
uint32_t p0 = (ruleLUT_u8[slice >> 56] << 6) |
              (ruleLUT_u8[(slice >> 48) & 0xFF] << 4) |
              (ruleLUT_u8[(slice >> 40) & 0xFF] << 2) |
              ruleLUT_u8[(slice >> 32) & 0xFF];
```

**NEON vs Scalar:**
- Uses scalar LUT lookups (cache-efficient) rather than NEON table lookups
- Optimized register usage for byte packing
- Platform-specific prefetch tuning (locality hint 3)

---

## 6. Parallel Execution & Lock-Free Patterns

### 6.1 AtomicQueue Design

**Location:** `VLife.h:17-47`

```cpp
template<typename T, size_t MaxSize = 65536>
struct AtomicQueue {
    std::array<T, MaxSize> data;
    std::atomic<size_t> count{0};

    void push_back(T item) {
        size_t idx = count.fetch_add(1, std::memory_order_relaxed);
        assert(idx < MaxSize && "AtomicQueue overflow");
        data[idx] = item;
    }

    void clear() { count.store(0, std::memory_order_relaxed); }
    size_t size() const { return count.load(std::memory_order_relaxed); }
    bool empty() const { return size() == 0; }
    T* begin() { return data.data(); }
    T* end() { return data.data() + size(); }
};
```

#### Design Trade-offs

**Why Not std::vector?**
- `push_back()` may reallocate, requiring mutex protection
- Reallocation involves memory copy, causing latency spikes
- Lock contention grows with thread count

**Why Not Thread-Local Queues + Merge?**
- Common pattern: each thread has local queue, merge at phase end
- Merge cost: O(threads x avg_queue_size) copies
- For 8 threads with 1000 tiles each: 8000 pointer copies per generation

**AtomicQueue Advantages:**
- **Single atomic per push:** `fetch_add` is lock-free on all modern CPUs
- **Zero merge cost:** All tiles already in single array
- **Iteration:** Direct pointer arithmetic, cache-friendly
- **Clear cost:** Single atomic store (O(1))

#### Memory Layout & Cost

```
+------------------------------------------------+
|  count (8 bytes, atomic)                        |
+------------------------------------------------+
|  data[0]    data[1]    data[2]    ...          |
|  (Tile*)    (Tile*)    (Tile*)                 |
|                                                 |
|  ... data[65535]                               |
+------------------------------------------------+
|  Total: 8 + 65536x8 = 524,296 bytes (~512KB)   |
+------------------------------------------------+
```

**Fixed Allocation Trade-off:**
- **Pro:** No runtime allocation, predictable memory
- **Pro:** No reallocation latency during hot path
- **Con:** 512KB per queue (1MB for double-buffered pair)
- **Con:** Max 65536 tiles per queue (sufficient for ~67M cells)

#### Contention Analysis

**fetch_add Contention:**
```
Thread 0: fetch_add returns 0, writes data[0]
Thread 1: fetch_add returns 1, writes data[1]  <- No conflict
Thread 2: fetch_add returns 2, writes data[2]  <- No conflict
```

- `fetch_add` contention: Single cache line bounces between cores
- Data write contention: **Zero** - each thread writes different index
- Expected cost: ~10-50 cycles per push (dominated by atomic)

**Comparison with Mutex-Protected Vector:**
| Operation | AtomicQueue | Mutex + Vector |
|-----------|-------------|----------------|
| Push (uncontended) | ~20 cycles | ~40 cycles |
| Push (8 threads) | ~50 cycles | ~200+ cycles |
| Clear | ~5 cycles | ~50+ cycles |
| Memory predictability | Constant | Variable |

### 6.2 Double-Buffered Queue Rotation

**Location:** `VLife.h:228-230`, `VLife.cpp:160-163`

```cpp
// Two queues for double-buffering
AtomicQueue<Tile*> phase1Queues[2];
AtomicQueue<Tile*>* phase1Queue{&phase1Queues[0]};
AtomicQueue<Tile*>* nextPhase1Queue{&phase1Queues[1]};

// Zero-copy swap between generations
inline void swapPhase1Queues() {
    phase1Queue->clear();
    std::swap(phase1Queue, nextPhase1Queue);
}
```

**Generation Flow:**
```
Generation N:
  Phase 1: Process tiles from phase1Queue (built in Gen N-1)
  Phase 2: Add tiles to nextPhase1Queue (for Gen N+1)

Generation N+1:
  Swap: phase1Queue <-> nextPhase1Queue (pointer swap, O(1))
  Phase 1: Process tiles from phase1Queue (now former nextPhase1Queue)
  ...
```

**Benefits:**
- No allocation/deallocation between generations
- Queue swap is 2 pointer assignments
- Previous queue's clear() happens after swap (amortizes across Phase 1)

### 6.3 Tile Queue Flag Deduplication

**Location:** `Tile.h:42-44`, `Tile.h:408-417`

```cpp
std::atomic<uint8_t> queuedForPhase1{0};

inline bool tryQueueForPhase1() {
    uint8_t expected = 0;
    return queuedForPhase1.compare_exchange_strong(expected, 1,
        std::memory_order_relaxed, std::memory_order_relaxed);
}

inline void clearQueueFlag() {
    queuedForPhase1.store(0, std::memory_order_relaxed);
}
```

**Why This Matters:**
- A tile can be modified by multiple neighbors during Phase 2
- Without deduplication: same tile queued multiple times
- AtomicQueue would fill with duplicates, wasting Phase 1 cycles

**CAS-Based Deduplication:**
```
Thread A: tryQueueForPhase1() -> CAS(0->1) succeeds -> returns true -> pushes tile
Thread B: tryQueueForPhase1() -> CAS(0->1) fails (already 1) -> returns false -> no push
```

**Memory Ordering:**
- `memory_order_relaxed` is sufficient because:
  - Flag only needs to prevent duplicate pushes (not synchronize data)
  - Phase boundary provides happens-before relationship
  - Flag cleared at Phase 1 start (before any Phase 2 operations)

### 6.4 Sequential Path Queue

**Location:** `VLife.h:221`, `VLife.cpp:288-294`

```cpp
std::vector<Tile*> changedTilesSequential;  // Simple vector for sequential path

// Sequential Phase 1
for (Tile* tile : *phase1Queue) {
    tile->clearQueueFlag();
    if (tile->runGenerationPrepare()) {
        changedTilesSequential.push_back(tile);
    }
}
```

**Why Separate Container:**
- Sequential path has no concurrent producers
- Vector's `push_back` is faster than atomic fetch_add
- Can use reserve() for amortized O(1) push
- No fixed size limit (can grow if needed)

### 6.5 Compare-and-Swap for Neighbor Updates

**Location:** `Tile.cpp:1287-1302`

```cpp
do {
    oldVal = __atomic_load_n(&cells[cellIdx], __ATOMIC_RELAXED);
    int count = (oldVal & mask) >> bitPos;
    count += delta;
    newVal = (oldVal & ~mask) | (static_cast<uint64_t>(count & 0x7) << bitPos);
} while (!__atomic_compare_exchange_n(&cells[cellIdx], &oldVal, newVal, ...));
```

**CAS Loop Characteristics:**
- Typically completes in 1-2 iterations
- Contention is low (boundary cells only, ~12% of total)
- Uses relaxed memory ordering (sufficient for Phase 2)

### 6.6 Thread-Local Metrics Counters

**Location:** `VLifeMetrics.h:23-44`

```cpp
struct ThreadLocalCounters {
    uint64_t cellsBorn{0};
    uint64_t cellsDied{0};
    uint64_t boundaryCrossings{0};
    uint64_t atomicOperations{0};
};
static thread_local ThreadLocalCounters tlCounters;
```

**Avoids:** Atomic contention during parallel accumulation
**Merge Point:** Phase boundaries (single sequential merge)

### 6.7 Reader-Writer Lock for Tile Map

**Location:** `VLife.h:211`, `VLife.cpp:51-111`

```cpp
mutable std::shared_mutex tilesMutex;

// Fast path: multiple concurrent readers
std::shared_lock<std::shared_mutex> readLock(tilesMutex);

// Slow path: exclusive for tile creation
std::unique_lock<std::shared_mutex> writeLock(tilesMutex);
```

---

## 7. Algorithmic Optimizations

### 7.1 Phase 1/Phase 2 Queue System

**Location:** `VLife.cpp:249-411`, `VLife.h:218-230`

**Queue-Based Processing:**
- **Without queue:** Must scan all N tiles every generation
- **With queue:** Only process tiles with modifications

**Double Buffering:**
```cpp
AtomicQueue<Tile*> phase1Queues[2];
AtomicQueue<Tile*>* phase1Queue{&phase1Queues[0]};
AtomicQueue<Tile*>* nextPhase1Queue{&phase1Queues[1]};
```

**Complexity Improvement:**
- Naive: O(N) where N = total tiles
- Queue-based: O(k) where k = active tiles
- **For sparse patterns: 10-100x reduction**

### 7.2 Hierarchical Modification Tracking

**Location:** `Tile.h:25-36`, `Tile.cpp:213-232`

Three-level skip hierarchy:

1. **wasModified (64 bits):** 8x8 blocks of 4x4 cells
2. **rowChangeMask (32 bits):** Per-row change tracking
3. **activityRows (8 bits):** Per-block-row content tracking

```cpp
// Phase 1: Skip unmodified blocks
uint8_t rowMask = (modMask >> (blockRow * 8)) & 0xFF;
if (rowMask == 0) continue;  // Skip 64 bytes of cells

// Phase 2: Skip unchanged rows
while (remainingRows != 0) {
    int localY = __builtin_ctz(remainingRows);
    ...
}
```

### 7.3 Lazy Tile Eviction

**Location:** `VLife.h:243-251`, `VLife.cpp:459-475`

**Eviction Criteria:**
```cpp
inline bool isSafeToEvict() const {
    return liveCount == 0 && activityRows == 0 && wasModified == 0;
}
```

**Cost Amortization:**
- Check every 128 generations
- Amortized cost: O(N/128) per generation
- Uses bitmask for fast modulo: `generationNumber & 127`

---

## 8. Optimization Interdependencies

### 8.1 Dependency Graph

```
+-------------------------------------------------------------+
|                     Nibble Encoding                          |
|                          |                                   |
|            +-------------+-------------+                     |
|            v             v             v                     |
|      Rule LUT      Delta LUT    Toggle Mask LUT              |
|            |             |             |                     |
|            +-------------+-------------+                     |
|                          v                                   |
|              Phase 1/Phase 2 Separation                      |
|                          |                                   |
|            +-------------+-------------+                     |
|            v             v             v                     |
|     wasModified    rowChangeMask    AtomicQueue              |
|     Tracking       Optimization     Lock-Free                |
|            |             |             |                     |
|            +-------------+-------------+                     |
|                          v                                   |
|                  Parallel Execution                          |
|                     (TBB)                                    |
+-------------------------------------------------------------+
```

### 8.2 Critical Interdependencies

| Optimization A | Depends On | Reason |
|----------------|------------|--------|
| Rule LUT | Nibble encoding | Index format matches cell byte layout |
| AVX-512 | Nibble encoding | SIMD computation mirrors nibble structure |
| AtomicQueue | Phase separation | Queue built during Phase 2, consumed in Phase 1 |
| rowChangeMask | wasModified | Row changes only tracked for modified blocks |
| Batched atomics | Corner mask LUT | Pre-accumulates masks before atomic ops |

### 8.3 Mutual Exclusions

| Optimization | Mutually Exclusive With | Reason |
|--------------|-------------------------|--------|
| AVX-512 native | SIMDE emulation | Build-time selection |
| UseAtomics=true | UseAtomics=false | Template instantiation |
| Parallel path | Sequential path | Runtime selection by tile count |

---

## 9. Code Path Specific Optimizations

### 9.1 Parallel Path Only

| Optimization | Location | Purpose |
|--------------|----------|---------|
| AtomicQueue | `VLife.h:17-47` | Lock-free tile queuing |
| CAS neighbor updates | `Tile.cpp:1287-1302` | Thread-safe boundary cells |
| Thread-local metrics | `VLifeMetrics.h:132` | Avoid atomic contention |
| TBB parallel_for_each | `VLife.cpp:354-384` | Work distribution |

### 9.2 Sequential Path Only

| Optimization | Location | Purpose |
|--------------|----------|---------|
| std::vector queue | `VLife.h:221` | Simple push_back |
| Direct memory writes | `Tile.cpp:1354-1367` | No CAS overhead |
| Non-atomic boundary | `Tile.h:113` | ~10-20% faster |

### 9.3 AVX-512 Path Only

| Optimization | Location | Purpose |
|--------------|----------|---------|
| 64-byte processing | `Tile.cpp:627` | 8x wider than scalar |
| Mask-based logic | `Tile.cpp:655-728` | Replace LUT lookups |
| Direct rule computation | `Tile.cpp:588-784` | No memory dependencies |

### 9.4 ARM NEON Path Only

| Optimization | Location | Purpose |
|--------------|----------|---------|
| Locality hint 3 | `Tile.cpp:15` | Temporal prefetch |
| Byte-packed LUT | `Tile.cpp:283-290` | Efficient scalar lookups |

---

## 10. Quantitative Analysis

### 10.1 Memory Access Costs

| Operation | Accesses | Bytes | Cache Lines |
|-----------|----------|-------|-------------|
| Phase 1 tile check | 1 | 8 | 1 (hot metadata) |
| Phase 1 full scan | 8-64 | 64-512 | 1-8 |
| Phase 2 per change | 2-4 | 8-32 | 1-4 |
| Cross-tile delta | 1-2 | 8-16 | 1-2 |

### 10.2 Branch Prediction Impact

| Pattern | Branches/Cell Pair | Mispredictions | Cycles Saved |
|---------|-------------------|----------------|--------------|
| Naive implementation | 8-10 | ~2-3 | 0 |
| LUT-based | 0-1 | ~0.1 | 30-45 |
| Branchless bit ops | 0 | 0 | 45-60 |

### 10.3 LUT Cache Efficiency

| LUT | L1 Hit Rate | Access Pattern |
|-----|-------------|----------------|
| ruleLUT | >99% | Sequential bytes |
| deltaLUT | >99% | 16 entries, 96 bytes |
| toggleMaskLUT | >99% | 32 entries, hot |
| stateDeltaLUT | >99% | 16 entries, 64 bytes |
| compactCornerMaskLUT | ~95% | Dependent on cell position |

### 10.4 Parallel Scalability

| Threads | Expected Speedup | Limiting Factor |
|---------|------------------|-----------------|
| 1 | 1.0x | Baseline |
| 2 | 1.8-1.9x | AtomicQueue contention minimal |
| 4 | 3.2-3.6x | CAS loop retries at boundaries |
| 8 | 5.5-6.5x | Memory bandwidth |
| 16+ | 7-10x | L3 cache contention |

### 10.5 Expected Overall Speedups

| Optimization Category | Speedup Range | Conditions |
|----------------------|---------------|------------|
| LUT-based rule evaluation | 2-3x | vs naive branches |
| Phase 1/2 queue system | 10-100x | Sparse patterns |
| Hierarchical skip | 2-5x | <50% cell density |
| AVX-512 (Phase 1) | 2-4x | On supported hardware |
| Parallel execution | 4-8x | 4-8 threads |
| Combined | 20-200x | vs naive implementation |

---

## Appendix: File Reference

| File | Key Optimizations |
|------|-------------------|
| `Tile.h:15-62` | Cache-aligned layout, modification masks |
| `Tile.h:287-398` | Batched corner marking |
| `Tile.cpp:213-574` | Phase 1 with hierarchical skipping |
| `Tile.cpp:576-785` | AVX-512 implementation |
| `Tile.cpp:820-1158` | Phase 2 with LUT optimization |
| `VLife.h:17-47` | AtomicQueue lock-free design |
| `VLife.h:253-258` | LUT declarations |
| `VLife.cpp:477-756` | LUT population |
| `VLifeMetrics.h:23-44` | Thread-local counters |

---

## Recommendations for Benchmarking

### macOS Profiling Commands

**Basic Timing:**
```bash
# Simple wall-clock timing
time ./build/VLifeBenchmark --quick

# More detailed timing with resource usage
/usr/bin/time -l ./build/VLifeBenchmark --quick 2>&1 | grep -E "(real|user|sys|maximum resident|page faults)"
```

**CPU Sampling with Instruments:**
```bash
# Time Profiler - CPU usage and call stacks
xcrun xctrace record --template "Time Profiler" --launch -- ./build/VLifeBenchmark --quick

# Open the resulting .trace file
open *.trace
```

**Command-line CPU Sampling:**
```bash
# Sample a running process (run benchmark first, then in another terminal)
./build/VLifeBenchmark &
sample $(pgrep VLifeBenchmark) 5 -file profile.txt

# Or sample during launch (5 seconds, 1ms interval)
sample -wait ./build/VLifeBenchmark 5 1 -file profile.txt
```

**Instruments Templates for Specific Metrics:**
```bash
# Cache/Memory analysis (requires running as root or with SIP adjustments)
sudo xcrun xctrace record --template "Allocations" --launch -- ./build/VLifeBenchmark --quick

# Thread analysis for parallel performance
xcrun xctrace record --template "System Trace" --launch -- ./build/VLifeBenchmark --quick

# List all available templates
xcrun xctrace list templates
```

**DTrace for Custom Metrics (requires disabling SIP or using entitled binaries):**
```bash
# Count function calls
sudo dtrace -n 'pid$target::*runGenerationPrepare*:entry { @calls[probefunc] = count(); }' -c ./build/VLifeBenchmark

# Measure function duration
sudo dtrace -n 'pid$target::*runGeneration*:entry { self->ts = timestamp; }
               pid$target::*runGeneration*:return /self->ts/ { @time[probefunc] = quantize(timestamp - self->ts); self->ts = 0; }' \
    -c ./build/VLifeBenchmark
```

**Built-in VLife Metrics (recommended):**
```bash
# Build with metrics enabled
cd build
cmake -DENABLE_METRICS=ON ..
make

# Run metrics benchmark with detailed output
./VLifeMetricsBenchmark --pattern acorn --generations 5000 --output acorn_metrics

# Output files: acorn_metrics.csv and acorn_metrics.json
```

**Parallel Scaling Test:**
```bash
# Test with different thread counts (TBB respects this)
for threads in 1 2 4 8; do
    echo "=== $threads threads ==="
    TBB_NUM_THREADS=$threads ./build/VLifeBenchmark --quick
done
```

**Memory Profiling:**
```bash
# Track allocations with leaks tool
leaks --atExit -- ./build/VLifeBenchmark --quick

# Memory graph (shows object relationships)
heap ./build/VLifeBenchmark

# Malloc debugging
MallocStackLogging=1 ./build/VLifeBenchmark --quick
```

**Activity Monitor CLI:**
```bash
# Monitor CPU/memory in real-time while benchmark runs
./build/VLifeBenchmark &
top -pid $(pgrep VLifeBenchmark) -l 10 -s 1
```

### Linux Profiling Commands (for reference)

```bash
# Cache metrics
perf stat -e cache-misses,cache-references ./build/VLifeBenchmark

# Branch mispredictions
perf stat -e branch-misses,branches ./build/VLifeBenchmark

# Cycle counts
perf stat -e cycles,instructions ./build/VLifeBenchmark

# SIMD utilization (AVX-512)
perf stat -e fp_arith_inst_retired.512b_packed_single ./build/VLifeBenchmark

# Full CPU counters
perf stat -d ./build/VLifeBenchmark
```

### What to Measure

| Metric | macOS Tool | What It Shows |
|--------|------------|---------------|
| Wall-clock time | `time` | Overall performance |
| CPU utilization | Instruments Time Profiler | Hot functions, parallel efficiency |
| Memory bandwidth | `/usr/bin/time -l` (page faults) | Cache pressure |
| Allocations | Instruments Allocations | Memory churn |
| Thread contention | Instruments System Trace | Lock contention, scheduling |
| Function call counts | `sample` or DTrace | Algorithm behavior |

### Suggested Benchmark Patterns

- **Sparse:** Glider gun (k/N ~ 1-5%)
- **Dense:** Random soup (k/N ~ 10-20%)
- **Static:** Still-life block (k/N = 0%)
- **Expanding:** Acorn (k/N varies over time)

### Quick Performance Comparison Script

```bash
#!/bin/bash
# Save as benchmark_compare.sh

echo "Building optimized version..."
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(sysctl -n hw.ncpu)

echo ""
echo "=== Quick Benchmark ==="
/usr/bin/time -l ./VLifeBenchmark --quick 2>&1 | head -20

echo ""
echo "=== Parallel Scaling ==="
for t in 1 2 4 8; do
    echo -n "Threads=$t: "
    TBB_NUM_THREADS=$t ./VLifeBenchmark --quick 2>&1 | grep -E "(generations|cells)"
done

echo ""
echo "=== With Metrics ==="
cmake -DENABLE_METRICS=ON ..
make -j$(sysctl -n hw.ncpu)
./VLifeMetricsBenchmark --pattern acorn --generations 1000 --output /tmp/bench
echo "Metrics saved to /tmp/bench.csv"
```

---

## 11. Apple Silicon Hardware Counter Profiling

VLife includes an optional hardware performance counter profiling system for Apple Silicon (M1/M2/M3) that provides cycle-accurate measurements of:

- **CPU Cycles**: Total clock cycles per generation
- **Instructions Retired**: Instructions executed
- **L1D Cache Misses**: Data cache miss count
- **Branch Mispredictions**: Branch prediction failures

### 11.1 Building with Hardware Counters

**Important:** `ENABLE_KPERF_COUNTERS` and `ENABLE_METRICS` are independent options. For accurate CPU counter profiling, do NOT enable `ENABLE_METRICS` as it adds per-generation overhead that will affect your measurements.

```bash
# Build with hardware counter support (recommended for profiling)
cd build
cmake -DENABLE_KPERF_COUNTERS=ON -DENABLE_METRICS=OFF ..
make VLifeCpuBenchmark
```

The benchmark will warn you if `ENABLE_METRICS` is also enabled.

### 11.2 Running the CPU Counter Benchmark

Hardware counter access requires elevated privileges on macOS:

```bash
# Run with sudo for hardware counter access
sudo ./VLifeCpuBenchmark --pattern acorn --generations 1000 --output acorn_cpu

# Timing-only mode (no sudo required)
./VLifeCpuBenchmark --pattern acorn --generations 1000 --timing-only

# Parallel mode profiling
sudo ./VLifeCpuBenchmark --pattern soup --soup-size 512 --parallel
```

### 11.3 Command-Line Options

| Option | Description | Default |
|--------|-------------|---------|
| `--pattern <name>` | Pattern to simulate | acorn |
| `--generations <n>` | Generations to run | 1000 |
| `--warmup <n>` | Warmup generations | 50 |
| `--output <name>` | Output file base | cpu_metrics |
| `--verbose` | Show per-generation progress | off |
| `--interval <n>` | Progress report interval | 100 |
| `--timing-only` | Disable hardware counters | off |
| `--parallel` | Enable parallel execution | off (sequential) |
| `--seed <n>` | Random seed for soup | 42 |
| `--soup-size <n>` | Soup pattern size | 256 |
| `--soup-density <f>` | Soup density (0.0-1.0) | 0.3 |

### 11.4 CSV Output Format

The benchmark produces a CSV file with one row per generation:

```csv
generation,cycles,instructions,l1d_misses,branch_mispred,ipc,miss_rate_per_1k,mispred_rate_per_1k,duration_ns,live_cells,tiles
0,125431,312456,89,23,2.49,0.2848,0.0736,45123,156,4
1,134567,298765,67,18,2.22,0.2243,0.0602,48234,167,5
...
```

| Column | Description |
|--------|-------------|
| `generation` | Generation number |
| `cycles` | CPU cycles |
| `instructions` | Instructions retired |
| `l1d_misses` | L1 data cache misses |
| `branch_mispred` | Branch mispredictions |
| `ipc` | Instructions per cycle |
| `miss_rate_per_1k` | L1D misses per 1000 instructions |
| `mispred_rate_per_1k` | Mispredictions per 1000 instructions |
| `duration_ns` | Wall-clock nanoseconds |
| `live_cells` | Population at generation end |
| `tiles` | Number of active tiles |

### 11.5 Interpreting Results

**IPC (Instructions Per Cycle):**
- > 2.5: Good for compute-bound code
- 1.5-2.5: Typical for mixed workloads
- < 1.5: Likely memory or branch bound

**L1D Miss Rate (per 1000 instructions):**
- < 5: Excellent cache utilization
- 5-20: Normal for data-intensive code
- > 20: Significant cache pressure

**Branch Misprediction Rate (per 1000 instructions):**
- < 2: Excellent branch predictability
- 2-10: Normal for typical code
- > 10: Consider branchless alternatives

### 11.6 Closed-Loop Profiling Workflow

The CPU counter benchmark enables a closed-loop optimization workflow:

```
1. CAPTURE: Run benchmark to collect baseline metrics
   sudo ./VLifeCpuBenchmark --pattern acorn --output /tmp/before

2. ANALYZE: Examine CSV for bottlenecks
   - High L1D miss rate in certain generations?
   - Low IPC correlated with tile count?
   - Misprediction spikes during pattern expansion?

3. OPTIMIZE: Make targeted code changes

4. VALIDATE: Re-run benchmark, compare results
   sudo ./VLifeCpuBenchmark --pattern acorn --output /tmp/after

5. COMPARE: Verify improvements
   # Example comparison (using pandas or similar)
   # before_avg_ipc = 2.1, after_avg_ipc = 2.6 -> 24% improvement
```

### 11.7 Combining with VLife Metrics

For comprehensive analysis, combine CPU counters with VLife metrics:

```bash
# Build with both metrics and counters
cmake -DENABLE_METRICS=ON -DENABLE_KPERF_COUNTERS=ON ..
make

# Run VLife metrics benchmark (high-level activity metrics)
./VLifeMetricsBenchmark --pattern acorn --generations 5000 --output acorn_activity

# Run CPU counter benchmark (low-level CPU metrics)
sudo ./VLifeCpuBenchmark --pattern acorn --generations 5000 --output acorn_cpu

# Cross-reference: correlate high phase2_time in activity metrics
# with high L1D miss rate in CPU metrics to identify cache-bound generations
```

### 11.8 Requirements and Limitations

**Requirements:**
- macOS on Apple Silicon (M1/M2/M3)
- sudo access for kperf framework
- Build with `-DENABLE_KPERF_COUNTERS=ON`

**Limitations:**
- Hardware counter API is private (may change between macOS versions)
- Counter access requires elevated privileges
- Sequential mode recommended for accurate single-thread profiling
- Parallel mode measures aggregate counters (may include thread scheduling noise)

### 11.9 Troubleshooting

**"kpc_force_all_ctrs_set failed - requires sudo":**
Run with `sudo` or ensure your user has the necessary entitlements.

**Zero counter values:**
- Verify Apple Silicon hardware (not Intel Mac)
- Check macOS version compatibility
- Try running without other profiling tools active

**Counters unavailable but timing works:**
Fall back to `--timing-only` mode for wall-clock measurements without hardware counters.

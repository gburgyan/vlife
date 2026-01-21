# VLife Optimization Experiments

This document records optimization attempts, including those that did not yield expected improvements. Understanding failed optimizations is as valuable as successful ones for guiding future work.

---

## Extended State Encoding (January 2025)

**Status: Reverted - 10% performance regression**

### Hypothesis

Phase 1 (`runGenerationPrepare`) computes which cells change state but only stores 1 bit per cell (changed/unchanged). Phase 2 (`runGenerationChanges`) must then re-read the cell's alive state from `cells[]` and recompute whether it was a birth or death. This appeared to be redundant work that could be eliminated.

### Proposed Solution

Store 2 bits per cell (4 bits per cell pair) instead of 1 bit per cell:
- `00` = unchanged
- `01` = born (was dead, now alive)
- `10` = died (was alive, now dead)

This encoding is exactly the `deltaLUT` index, so Phase 2 could use it directly without:
1. Re-reading `cells[]` to get the alive state
2. Computing `leftState = (1 + wasAlive) * changed`
3. Building the LUT index from scratch

### Implementation

Changes were made to:

| File | Change |
|------|--------|
| `Tile.h` | `TILE_CHANGE_64S` from `TILE_CELLS/64` to `TILE_CELLS/32` |
| `VLife.cpp` | `populateRuleLUT()` outputs 4-bit state encoding |
| `Tile.cpp` | Scalar `runGenerationPrepare()` packs 4 bits per pair |
| `Tile.cpp` | NEON `runGenerationPrepare()` packs 4 bits per pair |
| `Tile.cpp` | AVX-512 `runGenerationPrepare()` packs 4 bits per pair |
| `Tile.cpp` | `runGenerationChanges()` uses state directly |

### Expected Benefits

- Eliminate `cells[]` re-read in Phase 2 (cache miss avoidance)
- Eliminate 2 multiplications + 2 additions per changed cell pair
- Eliminate lutIndex recomputation
- Estimated ~10-15% speedup in Phase 2, ~5-10% overall

### Actual Results

**~10% performance regression** across all benchmark patterns.

### Analysis

The optimization traded computation for memory bandwidth, and memory bandwidth lost:

1. **Doubled `changes[]` array size**: 128 → 256 bytes per tile (+100% for this array, +17% total tile memory). This means:
   - Phase 1 writes 2x more data to `changes[]`
   - Phase 2 reads 2x more data from `changes[]`
   - More cache lines touched per tile

2. **The "saved" computation was cheap**: The redundant work we eliminated:
   ```cpp
   int leftWasAlive = (cells[currentCellIdx] >> leftCellBitPos) & 1;
   int leftState = (1 + leftWasAlive) * leftChanged;
   ```
   These are register-to-register operations (shift, AND, add, multiply) that execute in ~1-2 cycles on modern CPUs. The `cells[]` read was already in L1 cache since we immediately XOR into it.

3. **Memory bandwidth is the bottleneck**: On Apple Silicon (and most modern architectures), arithmetic is essentially free compared to memory access. The optimization increased memory traffic to save computation, which is backwards.

4. **Cache line granularity**: Even though we only read a few bytes per cell pair, memory is accessed in 64-byte cache lines. Doubling the `changes[]` size doubled the cache footprint during the critical inner loop.

### Lessons Learned

1. **Profile before optimizing**: The assumption that Phase 2's "redundant" computation was significant was not validated with profiling.

2. **Memory bandwidth > ALU ops**: On modern CPUs, especially Apple Silicon, memory bandwidth is far more precious than arithmetic operations. Optimizations that trade memory for compute usually lose.

3. **Cache effects dominate**: The `cells[]` array was already hot in L1 cache because Phase 2 immediately writes to it. The "redundant read" was essentially free.

4. **Measure, don't assume**: The ~10% regression was unexpected. Always benchmark before and after, ideally with multiple patterns.

### When This Approach Might Work

This optimization might be beneficial on architectures where:
- Memory bandwidth is relatively cheaper (older architectures, systems with very fast memory)
- The computation being saved is more expensive (e.g., if we had to do a full LUT lookup instead of a register shift)
- The `changes[]` array is not in the critical path for cache pressure

### Code Reference

The implementation was completed and tested (all tests pass) but reverted due to performance regression. The approach is documented here for future reference if revisiting under different circumstances.

---

## Prefetch Warmup (January 2026)

**Status: Reverted - ~1% performance regression**

### Hypothesis

The prefetch pattern in `runGenerationPrepare()` prefetches n+2 iterations ahead, meaning iterations 0-1 execute with cold cache. Adding warmup prefetches before the loop should eliminate initial cache misses and improve performance.

### Proposed Solution

Two changes were attempted:
1. Add `PREFETCH(&cells[0])` and `PREFETCH(&cells[4])` before both ARM64 and scalar loops to warm the cache before iteration starts
2. Change scalar path from n+1 to n+2 prefetch distance (to match ARM64 path for consistency)

### Implementation

Changes were made to `src/vlife/Tile.cpp`:

| Location | Change |
|----------|--------|
| ARM64/NEON path (~line 271) | Added warmup prefetches before loop |
| Scalar fallback (~line 358) | Added warmup prefetches, changed `i+4` to `i+8` |

### Expected Benefits

- Eliminate cache misses for first 1-2 loop iterations
- Consistent prefetch distance across code paths
- Estimated 1-2% improvement in Phase 1

### Actual Results

**~1% slower** on Apple Silicon (M-series)

### Analysis

The optimization failed for several interconnected reasons:

1. **Data was already hot**: When `runGenerationPrepare()` is called, the Tile object was just accessed (activity mask check, method dispatch via vtable). The `cells` array is a member of the Tile struct, so `cells[0]` was likely already pulled into L1 cache as part of the object access.

2. **Apple Silicon's hardware prefetcher is excellent**: M-series chips have sophisticated sequential prefetchers that were already handling the access pattern optimally. Manual prefetches can actually interfere with hardware prefetch pattern detection by adding noise to the access pattern.

3. **Prefetch overhead isn't zero**: Even prefetching data that's already cached incurs costs:
   - Pipeline slots for the prefetch instruction
   - Memory subsystem bandwidth to check cache tags
   - Potential L1 TLB pressure

4. **n+2 may be too far for scalar**: The scalar path's original n+1 distance was tuned for its specific loop body timing. Changing to n+2 may cause data to be evicted before use on some cache hierarchies, or simply waste prefetch bandwidth.

### Lessons Learned

1. **Don't prefetch what's already hot**: Before adding prefetches, verify the data isn't already cached from prior access patterns. Object member access often brings in adjacent data.

2. **Trust modern hardware prefetchers**: Apple Silicon (and other modern CPUs) have highly optimized hardware prefetchers for simple sequential access patterns. Manual prefetches should only be added when you have evidence the hardware is missing.

3. **Different prefetch distances for different architectures**: The ARM64 path with NEON can handle n+2 prefetch distances, but scalar code with different loop body timing may work better with n+1.

4. **Measure small optimizations carefully**: A ~1% regression is within noise for many benchmarks. Multiple runs and statistical analysis are needed to confirm such small effects.

### When This Approach Might Work

Warmup prefetches might help when:
- The loop is entered from a cold path (not immediately after touching the object)
- Hardware prefetchers are disabled or ineffective (older CPUs, non-sequential patterns)
- The prefetched data is not part of the recently-accessed object

---

## Template for Future Experiments

### [Optimization Name] (Date)

**Status: [Success/Reverted/Partial]**

#### Hypothesis
What inefficiency was identified and why did we think it could be improved?

#### Proposed Solution
What changes were proposed?

#### Implementation
What files were changed?

#### Expected Benefits
What performance improvement was anticipated?

#### Actual Results
What actually happened?

#### Analysis
Why did it succeed/fail?

#### Lessons Learned
What can we apply to future optimization attempts?

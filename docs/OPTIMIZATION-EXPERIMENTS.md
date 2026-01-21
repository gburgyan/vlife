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

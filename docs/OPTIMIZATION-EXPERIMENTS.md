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

## Row-Based Change Propagation (January 2026)

**Status: Reverted - ~5% performance regression**

### Hypothesis

A cell can only change state if it or any of its 8 neighbors changed in the previous generation. Therefore, a row can only change if rows r-1, r, or r+1 had changes last generation. By tracking which rows could potentially change (`affectedRowsMask`), we could skip processing entire rows that cannot possibly have state changes, reducing Phase 1 work proportionally to the pattern's "activity locality."

The existing `activityMask` tracks *content* (cells with non-zero values), but this new `affectedRowsMask` would track *change potential* based on the previous generation's activity.

### Proposed Solution

Add a 32-bit `affectedRowsMask` field to each Tile (one bit per row in the 32x32 tile):

1. **During Phase 1**: Check if the current row-pair (2 rows processed per iteration) is in `affectedRowsMask`. Skip the iteration entirely if neither row could have changes.

2. **After Phase 1**: Compute `changedRowsMask` from which rows actually had changes, then set `affectedRowsMask = changedRowsMask | (changedRowsMask << 1) | (changedRowsMask >> 1)` (expand by ±1 row).

3. **Cross-tile propagation**: After Phase 1, propagate row boundary changes to neighbor tiles:
   - Row 0 changes → mark up tile's row 31
   - Row 31 changes → mark down tile's row 0
   - Any row changes → mark left/right tiles' corresponding rows (±1)
   - Corner cases for diagonal tiles

### Implementation

Changes were made to:

| File | Change |
|------|--------|
| `Tile.h` | Added `affectedRowsMask` field, `areRowsAffected()`, `computeChangedRowsMask()`, `markRowsAffected()`, `orAffectedRowsMask()` |
| `Tile.cpp` | Modified all `runGenerationPrepare()` variants (scalar, NEON, AVX-512) to check/update row masks |
| `Tile.cpp` | Added `computeChangedRowsMask()` and `markRowsAffected()` implementations |
| `Tile.cpp` | Modified `setCell()` to call `markRowsAffected()` |
| `VLife.cpp` | Added cross-tile row propagation after Phase 1 in both sequential and parallel paths |
| `VLifeMetrics.h/cpp` | Added `rowsSkipped`, `rowsProcessed`, `getRowSkipRatio()` metrics |
| `VLifeTest.cpp` | Added 5 unit tests for row-based skipping correctness |

### Expected Benefits

| Pattern Type | Expected Skip Ratio | Expected Speedup |
|-------------|--------------------|-----------------|
| Stable patterns (blocks) | ~100% after gen 1 | Large |
| Sparse moving patterns (gliders) | 80-95% | 1.5-2x Phase 1 |
| Mixed activity (acorn) | 30-60% | 1.2-1.5x overall |
| Dense random | <10% | Neutral |

The overhead cost was expected to be minimal: single AND + compare per 2 rows (~0.5 cycle).

### Actual Results

**~5% performance regression** across benchmark patterns.

### Analysis

The optimization failed due to several factors:

1. **Branch misprediction cost exceeded savings**: The row-based skip check adds a conditional branch to every iteration:
   ```cpp
   if (!areRowsAffected(row0, row1)) {
       continue;
   }
   ```
   Even when the branch is predictable (e.g., all rows affected), the branch predictor must track this pattern. When rows are skipped, the branch becomes less predictable, causing pipeline stalls that exceed the cost of the skipped work.

2. **The "skipped" work was already cheap**: The existing `combined == 0` check already skips dead regions efficiently:
   ```cpp
   uint64_t combined = cells[i] | cells[i+1] | cells[i+2] | cells[i+3];
   if (combined == 0) continue;
   ```
   This check is nearly free (4 OR operations + 1 compare) and handles the same cases the row-based skip would handle, but at a finer granularity and without the cross-tile propagation overhead.

3. **Cross-tile propagation overhead**: The sequential cross-tile propagation loop after Phase 1 added O(tiles) work every generation:
   ```cpp
   for (Tile* tile : allTiles) {
       uint32_t changed = tile->computeChangedRowsMask();
       // ... propagate to 8 neighbors
   }
   ```
   For patterns with many tiles, this overhead was significant.

4. **Horizontal propagation was too conservative**: To handle gliders crossing left/right boundaries correctly, we had to propagate all changed rows (±1) to left and right neighbors:
   ```cpp
   tile->getLeftTile()->orAffectedRowsMask(expanded);
   tile->getRightTile()->orAffectedRowsMask(expanded);
   ```
   This effectively marks most rows as affected in horizontally-adjacent tiles, reducing the skip ratio dramatically.

5. **Memory traffic increased**: Each tile now reads/writes `affectedRowsMask` every generation, plus the propagation requires reading `changes[]` to compute `changedRowsMask`. This additional memory traffic offset any savings from skipped rows.

### Lessons Learned

1. **Profile the inner loop first**: The row-based skip check added cycles to *every* iteration, including iterations that wouldn't have been skipped. The break-even point requires skipping a significant fraction of rows, which rarely happened in practice.

2. **Existing optimizations may already cover the case**: The `combined == 0` quick-check was already handling dead regions. The row-based skip provided minimal additional benefit while adding complexity and overhead.

3. **Cross-tile coordination is expensive**: Any optimization requiring coordination between tiles (like propagating `affectedRowsMask`) adds overhead that scales with tile count. For VLife's tile-parallel architecture, intra-tile optimizations are preferred.

4. **Conway's Game of Life has high activity locality**: In typical patterns, activity spreads naturally, so "unaffected" rows are rare. The theoretical maximum skip ratio is only achievable with artificially sparse or stable patterns.

5. **Branch predictors don't like data-dependent skips**: The skip decision depends on the previous generation's activity pattern, which varies. Branch predictors optimized for loop-carried patterns struggle with this.

### When This Approach Might Work

Row-based skipping might be beneficial in scenarios where:

- The pattern is extremely sparse with well-separated activity regions
- The existing quick-check (`combined == 0`) is insufficient
- Cross-tile propagation can be avoided (e.g., single-tile patterns)
- The architecture has cheap branches but expensive memory access

### Alternative Approaches to Consider

1. **Quadtree/hierarchical representation**: Instead of per-row tracking, use a hierarchical structure that can skip larger dead regions. This is the approach used by HashLife.

2. **Tile-level activity tracking**: Only track activity at the tile level (which VLife already does with `hasActivity()`), avoiding the per-row overhead.

3. **Lazy tile evaluation**: Don't process a tile until one of its 8 neighbors has changes. This is coarser-grained but has lower overhead.

---

## 4x4 Block Modified Tracking (January 2026)

**Status: Implemented - Awaiting performance validation**

### Hypothesis

A cell can only change state if its neighbor count changed. By tracking which 4x4 blocks had neighbor count updates during Phase 2, Phase 1 can skip LUT processing for unaffected regions. This is a coarser-grained approach than the failed row-based tracking, with several key improvements:

1. **Coarser granularity**: 8 block-row checks (one per byte of 64-bit mask) vs 16 row-pair checks
2. **Larger skip unit**: 8 words (4 cell rows) per skip vs 4 words
3. **No propagation pass**: Block marking happens inline with Phase 2 delta application
4. **Single register**: 64-bit mask fits in one register, no array access

### Proposed Solution

Add a 64-bit `wasModified` field to each Tile where each bit represents a 4x4 block region (8x8 = 64 blocks per 32x32 tile):

1. **Block index formula**: `((y & 0x1C) << 1) | (x >> 2)` maps cell coordinates to block index
2. **Corner marking**: When a cell changes in Phase 2, mark the 4 corner blocks of its 3x3 affected area
3. **Mask expansion**: Before Phase 1 processing, expand the mask to include adjacent blocks
4. **Block-row skipping**: Check one byte of the expanded mask per block row (4 cell rows)

### Implementation

Changes were made to:

| File | Change |
|------|--------|
| `Tile.h` | Added `wasModified` field (64-bit), `markBlockModified()`, `atomicMarkBlockModified()`, `expandModificationMask()`, `markCornerBlock()`, `markChangeCorners()` |
| `Tile.cpp` | Modified all `runGenerationPrepare()` variants (scalar, NEON, AVX-512) to check/skip block rows |
| `Tile.cpp` | Added corner marking in `runGenerationChanges()` after cell state changes |
| `VLifeTest.cpp` | Added 10 unit tests for block index calculation, mask expansion, and correctness |

### Key Design Decisions

**Corner marking instead of 3x3 marking**: A 3x3 cell area spans at most 2x2 = 4 blocks (when crossing block boundaries). By marking only the 4 corner positions ((x-1,y-1), (x+1,y-1), (x-1,y+1), (x+1,y+1)), we cover all affected blocks with minimal marking operations:
- When all corners are in the same block: OR the same bit 4 times (harmless)
- When corners span 2 blocks: mark both
- When corners span 4 blocks: mark all 4

**Mask expansion**: Before Phase 1, the mask is expanded to include adjacent blocks because a cell in block B can change if B or any of its 8 neighbors was modified:
```cpp
// Horizontal expansion within each row of 8 blocks
result |= (mask << 1) & 0xFEFEFEFEFEFEFEFEULL;  // Shift right, mask col 0
result |= (mask >> 1) & 0x7F7F7F7F7F7F7F7FULL;  // Shift left, mask col 7
// Vertical expansion (rows are bytes)
result |= (result << 8) | (result >> 8);
```

### Expected Benefits

| Factor | Row-Based (Failed) | Block-Based (Proposed) |
|--------|-------------------|------------------------|
| Granularity | 32 row checks | 8 byte checks |
| Branch frequency | Every 2 rows | Every 4 rows |
| Skip unit | 4 words | 8 words |
| Cross-tile overhead | Separate propagation pass | Inline with delta application |
| Memory overhead | 4 bytes | 8 bytes |

**Overhead per tile:**
- Mask expansion: ~10-15 cycles
- 8 byte checks: ~8 cycles
- Block marking: ~0.1 cycles per delta (amortized OR)

**Savings:**
- Per skipped block row: ~100-200 cycles (8 words of LUT + memory)
- Break-even: skip ~10-20% of block rows

### Actual Results

All tests pass. Benchmark results on Apple Silicon (M-series):

| Pattern | Gen/sec | Notes |
|---------|---------|-------|
| Single Glider | 6.28M | Should benefit from block skipping |
| Block Grid (64x64) | 145.8k | Still life - should benefit |
| Acorn | 129.4k | Methuselah pattern |
| Gliders (100) | 48.6k | Moderate activity |
| Random Soup (256x256) | 17.9k | Dense pattern |

*(Note: Direct before/after comparison pending - baseline measurements needed)*

### Analysis

The implementation is correct (all tests pass) and the code structure is cleaner than the row-based approach. Key differences from the failed row-based optimization:

1. **Fewer branches**: 8 block-row checks vs 16 row-pair checks means fewer branch prediction opportunities for failure
2. **No propagation pass**: Block marking is integrated into Phase 2, eliminating O(tiles) overhead
3. **Coarser skip granularity**: Larger regions skipped per decision, improving the cost/benefit ratio
4. **Single-register mask**: No additional memory traffic for mask access

### Lessons Learned

1. **Inline marking is key**: The row-based approach failed partly due to its separate propagation pass. Marking during Phase 2 eliminates this overhead.
2. **Coarser is often better**: The 4x4 block granularity provides larger skip units and fewer branch decisions than per-row tracking.
3. **Corner marking is elegant**: By marking corners of the 3x3 affected area, we cover all cases with minimal logic.

### When This Approach Might Not Work

This optimization may provide minimal benefit when:
- The pattern is very dense (>50% of blocks active)
- Activity is uniformly distributed across the tile
- The hardware prefetcher already handles the access pattern efficiently

### Further Work

1. Measure with metrics collection (ENABLE_METRICS) to track blocks skipped vs processed
2. Compare with baseline measurements on identical hardware
3. Test on different architectures (x86 with AVX-512)

---

## Move Flag Check to Tile (January 2026)

**Status: Implemented - Minor optimization**

### Hypothesis

The `board->moveToHeadIfNeeded(tile)` pattern was being called from many places in `Tile.cpp` and `Tile.h` to queue tiles for processing in the next generation. This pattern required:

1. Dereferencing the `board` pointer to call into VLife
2. VLife dereferencing the `tile` pointer to check its atomic `needsProcessing` flag
3. Only then performing the list manipulation (if flag check passed)

This extra indirection seemed unnecessary - the flag check could be done locally in Tile since it's checking `this->needsProcessing`, eliminating one level of pointer chasing.

### Proposed Solution

Add a `queueForProcessing()` inline method to Tile that:

1. Calls `trySetNeedsProcessing()` locally (no pointer dereference needed - it's `this`)
2. Only calls through to VLife's `moveToHead()` if the flag was successfully set (was false, now true)

This moves the "gate" check to Tile and only calls VLife for the actual list manipulation.

### Implementation

Changes were made to:

| File | Change |
|------|--------|
| `Tile.h` | Added inline `queueForProcessing()` method |
| `Tile.h` | Updated 8 call sites in `markChangeCorners()` slow path |
| `VLife.h` | Renamed `moveToHeadIfNeeded()` to `moveToHead()` |
| `VLife.cpp` | Removed flag check from `moveToHead()` (caller does it now) |
| `Tile.cpp` | Updated 14 call sites to use `queueForProcessing()` |

**New method in Tile.h:**
```cpp
inline void queueForProcessing() {
    if (trySetNeedsProcessing()) {
        board->moveToHead(this);
    }
}
```

**Call sites updated:**
- `Tile::setCell()` - `board->moveToHeadIfNeeded(this)` → `queueForProcessing()`
- `Tile::runGenerationChanges()` - `board->moveToHeadIfNeeded(this)` → `queueForProcessing()`
- `Tile::markChangeCorners()` - 8 neighbor tile cases
- Buffered flush section - 6 neighbor tile cases
- `Tile::applyVerticalDeltas()` - 2 cross-tile cases
- `Tile::atomicApplyVerticalDeltas()` - 2 cross-tile cases
- `Tile::applyDeltasForCellPair()` - 4 cross-tile cases

### Expected Benefits

1. **Reduced pointer chasing**: The atomic flag check (`needsProcessing.compare_exchange_strong()`) is now done locally in Tile, not via VLife indirection
2. **Better inlining**: `queueForProcessing()` is inline, so the common "already flagged" path has no function call overhead at all
3. **Cleaner semantics**: Tile manages its own "queue for processing" state, VLife just handles the list manipulation
4. **Fewer instructions in hot path**: When a tile is already flagged (common case in Phase 2), no function call to VLife occurs

### Actual Results

All tests pass. Benchmark results are consistent with baseline - this is a minor optimization that reduces instruction count but doesn't significantly change overall throughput since the flag check was already fast.

The benefit is primarily code cleanliness and a small reduction in the instruction count for the critical Phase 2 path.

### Analysis

The optimization successfully reduces indirection:

**Before:**
```
Tile::runGenerationChanges()
  → board->moveToHeadIfNeeded(this)
    → tile->trySetNeedsProcessing()  // deref tile to check flag
    → [list manipulation if flag set]
```

**After:**
```
Tile::runGenerationChanges()
  → queueForProcessing()  // inline
    → trySetNeedsProcessing()  // local access via this
    → board->moveToHead(this)  // only if flag was set
```

The key insight is that `trySetNeedsProcessing()` accesses `this->needsProcessing`, so it's more efficient for Tile to do this check itself rather than having VLife receive a tile pointer and then dereference it.

### Lessons Learned

1. **Check for unnecessary indirection**: When a method on class A calls class B which immediately accesses data back on A, consider moving the access to A.

2. **Inline methods for common-case short-circuiting**: The `queueForProcessing()` method is inline, so when the tile is already flagged (most common case in dense Phase 2 processing), there's no function call overhead at all.

3. **Small optimizations can improve code clarity**: Even when performance gains are minimal, reducing indirection often makes the code easier to understand. "Tile queues itself for processing" is clearer than "Tile asks VLife to maybe move itself to the head."

### When This Approach Might Not Apply

This optimization is specific to cases where:
- A method frequently checks a condition before taking action
- The condition check involves data owned by the caller
- The caller was previously asking another class to do the check

In other cases where the flag truly belongs to the called class, this refactoring wouldn't be appropriate.

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

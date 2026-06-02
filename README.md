# VLife

A high-performance **Conway's Game of Life** engine in C++20 whose cost scales with the *activity* of a pattern, not its size. A drifting glider runs millions of generations per second no matter how big the empty universe around it is.

> **The core idea:** traditional Life implementations recompute every cell's neighbor count every generation — `O(N)` work for `N` live cells. VLife stores neighbor counts *as part of the cell state* and touches only the cells that actually change. Cost becomes `O(k)`, where `k` is the number of state changes per generation. Since real patterns change only 1–10% of their cells per step, this is a large, structural win.

---

## Why this is neat

Most fast Life engines pick one trick and live with its trade-off. VLife layers several so that performance stays high *and predictable* across wildly different patterns.

- **Activity-proportional, not population-proportional.** A single glider (5 cells, ~1 change every few generations) and a 633-cell methuselah both run fast because both have low *activity*. What determines speed is `k` (changes/gen), not `N` (live cells). Measured spread is ~350× between a lone glider and a dense random soup — tracking the activity ratio almost exactly.

- **Incremental neighbor counts.** Each cell carries its own live-neighbor count. When a cell is born or dies, the engine adds ±1 to its 8 neighbors — it never rescans the grid. The expensive "count neighbors" step that dominates naive implementations simply doesn't exist here.

- **Two cells per byte, neighbor counts baked in.** Cells are packed as 4-bit nibbles: 1 bit of alive/dead + a 3-bit neighbor count (excluding the paired cell, which is recovered for free during the rule lookup). A 32×32 tile is 1024 cells in just **512 bytes**.

- **A 256-byte rule table does Conway's rules for a *pair* of cells at once.** One lookup decides births and deaths for two cells simultaneously. Several other small precomputed LUTs (toggle masks, state/delta, a y-symmetric "corner mask" compressed 16× from 8 KB to 512 bytes) turn the inner loop nearly branchless.

- **Sparse tiling with self-scheduling work queues.** The plane is divided into 32×32 tiles; only tiles that contain (or border) activity exist and get processed. A lock-free, double-buffered queue means a still-life pattern returns *immediately* — zero tiles processed. Dead tiles are lazily evicted and recycled from a TBB memory pool.

- **Genuinely parallel.** Work fans out across cores with Intel TBB. Phase 1 (rule evaluation) is read-only and fully parallel; only the ~12% of cells on tile boundaries need atomics in Phase 2. Cache-line-aligned tile layout keeps hot metadata off the cell data and avoids false sharing.

- **SIMD where it counts.** ARM **NEON** (Apple Silicon) and **AVX-512** (x86, via [SIMDE](https://github.com/simd-everywhere/simde) for portability) paths accelerate rule evaluation, selected at runtime by CPU feature detection.

### How it compares

| Approach | Strong at | Weak at |
|---|---|---|
| Naive dense array | Tiny fixed grids | Wastes work on dead space |
| Sparse hashmap | Very sparse patterns | Cache misses, hashing overhead |
| **HashLife** | Regular, repeating patterns (huge speedups) | Chaotic methuselahs (can be *slower* than naive) |
| GPU (CUDA/OpenCL) | Peak throughput on dense grids | Sparse patterns, transfer latency |
| **VLife** | **Predictable, bounded performance across *all* pattern types** | Very dense grids (still ~1.4B cell-updates/sec) |

VLife's sweet spot is real-time visualization and chaotic-pattern exploration, where *consistent low latency* matters more than peak throughput on a packed grid.

And the one weak spot — very dense grids — is largely self-correcting in practice. High density is unstable under Conway's rules: a cell with too many live neighbors dies of overcrowding, so a packed region thins out within a few generations toward the ~15–40% densities where Life actually lives. The worst case for VLife is therefore mostly transient; patterns naturally drift *toward* the activity-proportional regime where it excels.

---

## Quick start

**Requirements:** a C++20 compiler, [CMake](https://cmake.org/), and [Intel TBB](https://github.com/oneapi-src/oneTBB) (required). Qt5 is needed only for the GUI; tests and benchmarks build without it.

```bash
mkdir -p build && cd build
cmake ..
make
```

### Run the GUI

```bash
./GameOfLifeApp
```

A Qt-based viewer with run/stop, single-step, and zoom controls.

### Run the benchmarks

```bash
./VLifeBenchmark --quick    # fast smoke benchmark
./VLifeBenchmark            # full benchmark suite
```

Full-suite run on Apple Silicon (M-series, 18 hardware threads):

```
Pattern: Single Glider               2,853,827 gen/sec (0.4 us/gen)
Pattern: Acorn (methuselah)            155,883 gen/sec (6.4 us/gen)
Pattern: Glider Gun (10 guns)           72,787 gen/sec (13.7 us/gen)
Pattern: Spaceship Fleet (50 ships)     62,247 gen/sec (16.1 us/gen)
Pattern: Gliders (100 gliders)          60,479 gen/sec (16.5 us/gen)
Pattern: Random Soup (256², 30%)        27,333 gen/sec (36.6 us/gen)

Parallel vs Sequential (1024², ~1156 tiles):
  Sequential     396 gen/sec
  Parallel     3,305 gen/sec     ->  8.35x speedup
```

The lone glider runs **~47× faster** than 100 gliders and **~100× faster** than a dense soup — speed tracks activity, exactly as designed.

### Run the tests

```bash
ctest --output-on-failure
```

---

## Build options

| CMake option | Default | Purpose |
|---|---|---|
| `BUILD_UI` | `ON` | Build the Qt5 GUI (`GameOfLifeApp`). Turn off to skip the Qt dependency. |
| `ENABLE_AVX512` | `OFF` | Enable the AVX-512 rule-evaluation path. Uses SIMDE emulation, so it builds and runs correctly on **any** CPU (including ARM) — good for CI/testing. |
| `AVX512_NATIVE` | `OFF` | (Needs `ENABLE_AVX512=ON`) Use native AVX-512 intrinsics. The binary then **requires** an AVX-512 CPU (Intel Ice Lake+, AMD Zen 4+). |
| `ENABLE_METRICS` | `OFF` | Per-generation research metrics (`k`, `N`, boundary ops, timing). Zero overhead when off. |
| `ENABLE_KPERF_COUNTERS` | `OFF` | Apple Silicon hardware counters (cycles, IPC, cache/branch misses) via `kperf`. Needs `sudo` at runtime. |

```bash
# Portable AVX-512 (SIMDE, runs everywhere)
cmake -DENABLE_AVX512=ON ..

# Native AVX-512 (requires AVX-512 hardware at runtime)
cmake -DENABLE_AVX512=ON -DAVX512_NATIVE=ON ..
```

---

## Build targets

| Target | What it is |
|---|---|
| `GameOfLifeApp` | Qt5 GUI application |
| `GameOfLifeTests` | GoogleTest suite (run via `ctest`) |
| `VLifeBenchmark` | Core benchmark (`--quick` for a fast pass) |
| `VLifeExtendedBenchmark` | Extended patterns, density sweeps, activity tests |
| `VLifeMetricsBenchmark` | Research metrics collection (needs `-DENABLE_METRICS=ON`) — e.g. `--pattern acorn --generations 5000 --output acorn_data` |
| `VLifeCpuBenchmark` | Apple Silicon HW-counter profiling (needs `-DENABLE_KPERF_COUNTERS=ON`); `--timing-only` runs without `sudo` |

---

## How it works (in one minute)

Each generation runs in two phases over only the active tiles:

1. **Phase 1 — Prepare (read-only, fully parallel).** For every cell pair, a 256-entry lookup table reads the packed alive-bit + neighbor-count and decides which cells will flip. Tiles with no pending changes are skipped entirely; rows with no changes are skipped via a per-tile bitmask scanned with `ctz`.

2. **Phase 2 — Commit.** Flip the cells that changed, and propagate ±1 neighbor-count deltas to their 8 neighbors. Interior updates are plain writes; cross-tile boundary updates are batched and applied atomically, and the affected neighbor tiles enqueue themselves for next generation's Phase 1.

Because only changed cells and their neighbors are ever touched, the work per generation is proportional to `k`, the number of changes — the heart of the activity-proportional claim.

### Cell encoding

```
One byte = two horizontally-adjacent cells:

  bit 7      bits 6-4         bit 3      bits 2-0
 [alive] [neighbor count] | [alive] [neighbor count]
   Left cell                  Right cell
```

The 3-bit neighbor count omits the paired cell; its contribution is added back during the rule lookup. This lets one table lookup resolve both cells at once.

---

## Project layout

```
src/
  GameOfLife.h            Abstract interface
  SimpleGameOfLife.*      Reference sparse-hashmap implementation
  GameOfLifeView.*        Qt5 viewer
  vlife/
    VLife.*               Engine: tiles, work queues, two-phase generation
    Tile.*                Tile: cell storage, rule eval, NEON/AVX-512 paths
    TilePool.*            TBB-backed tile allocator/recycler
    CpuFeatures.h         Runtime CPU feature detection
    VLifeMetrics.*        Optional research metrics
    AppleSiliconCounters.* Optional kperf hardware counters
tests/
  vlife/                  GoogleTest correctness tests
  benchmark/              Benchmark tools and patterns
docs/                     Deep-dive documentation (below)
```

---

## Documentation

| Document | Contents |
|---|---|
| [`docs/vlife-technical-paper.md`](docs/vlife-technical-paper.md) | Full technical paper: complexity analysis, architecture, benchmarks, and comparison to HashLife / GPU / FPGA approaches |
| [`docs/PERFORMANCE-OPTIMIZATIONS.md`](docs/PERFORMANCE-OPTIMIZATIONS.md) | Optimization deep dive: cache layout, LUTs, SIMD, lock-free queues, branch-prediction tricks, profiling |
| [`docs/TILE-CELL-LIFECYCLE.md`](docs/TILE-CELL-LIFECYCLE.md) | Step-by-step walkthrough of tile/cell lifecycle, including a worked glider example |
| [`docs/METRICS.md`](docs/METRICS.md) | The metrics system: what it measures and how to use it to validate the `O(k)` claim |
| [`docs/OPTIMIZATION-EXPERIMENTS.md`](docs/OPTIMIZATION-EXPERIMENTS.md) | Log of optimizations tried — including the ones that *didn't* work, and why |
| [`docs/PROFILING.md`](docs/PROFILING.md) | Profiling VLife on macOS (Instruments, `sample`, hardware counters) |

---

## Glossary

- **`k`** — state changes per generation (births + deaths). The thing performance scales with.
- **`N`** — population (live cells).
- **Activity ratio (`k/N`)** — typically 1–10% once a pattern settles; the lower it is, the bigger VLife's advantage.
- **Tile** — a 32×32 cell region (1024 cells, 512 bytes), the unit of allocation and scheduling.
- **Nibble** — the 4-bit per-cell encoding (alive bit + 3-bit neighbor count).
- **CUpS** — cell-updates per second (`N × gen/sec`), a throughput measure that stays ~constant across densities.

# VLife Metrics Collection System

This document describes the optional metrics collection system for VLife, designed to support research paper claims about activity-proportional complexity.

**Related Documentation:**
- [PROFILING.md](PROFILING.md) - General profiling guide with Instruments and sample
- [PERFORMANCE-OPTIMIZATIONS.md](PERFORMANCE-OPTIMIZATIONS.md) - Optimization techniques and CPU counter profiling

## Overview

The metrics system collects detailed per-generation data including:
- **State changes (k)**: Cells born and died each generation
- **Population (N)**: Total live cells
- **Activity ratio (k/N)**: Core metric for proving O(k) complexity
- **Tile statistics**: Active tiles, boundary crossings, atomic operations
- **Timing breakdown**: Phase 1 (rule evaluation) and Phase 2 (neighbor updates)

The system is designed for **zero performance impact** when disabled—all instrumentation compiles to no-ops via preprocessor macros.

## Building with Metrics

### Enable Metrics Collection

```bash
mkdir build-metrics
cd build-metrics
cmake -DENABLE_METRICS=ON ..
make
```

### Verify Metrics are Enabled

When configured correctly, CMake will output:
```
-- Metrics collection enabled
-- Metrics benchmark: 'run_metrics_benchmark' target available.
```

### Build Without Metrics (Default)

```bash
mkdir build
cd build
cmake ..
make
```

When `ENABLE_METRICS` is not set, all metrics macros expand to `((void)0)`, ensuring zero runtime overhead.

## Using the Metrics Benchmark Tool

The `VLifeMetricsBenchmark` executable runs patterns and exports detailed metrics.

### Basic Usage

```bash
./VLifeMetricsBenchmark --pattern acorn --generations 5000 --output acorn_metrics
```

This produces:
- `acorn_metrics.csv` - Tabular data for analysis
- `acorn_metrics.json` - Structured data with metadata

### Command-Line Options

| Option | Description | Default |
|--------|-------------|---------|
| `--pattern <name>` | Pattern to simulate | `acorn` |
| `--generations <n>` | Number of generations to measure | `5000` |
| `--warmup <n>` | Warmup generations (no metrics) | `100` |
| `--output <name>` | Output filename base | `vlife_metrics` |
| `--csv-only` | Export only CSV | |
| `--json-only` | Export only JSON | |
| `--verbose` | Print per-generation details | |
| `--interval <n>` | Progress report interval | `500` |
| `--seed <n>` | Random seed for soup pattern | `42` |
| `--soup-size <n>` | Size of random soup | `256` |
| `--soup-density <f>` | Density 0.0-1.0 | `0.3` |
| `--help` | Show help message | |

### Available Patterns

| Pattern | Description |
|---------|-------------|
| `acorn` | Methuselah that grows then stabilizes (~5000 gens to settle) |
| `r-pentomino` | Classic methuselah pattern |
| `glider` | Single glider (minimal pattern) |
| `gliders` | 100 gliders moving diagonally |
| `guns` | 10 Gosper glider guns (sustained activity) |
| `soup` | Random initial configuration |
| `blocks` | 64×64 grid of still-life blocks (no changes) |
| `spaceships` | 50 LWSS spaceships |

### Examples

```bash
# Long run on acorn methuselah
./VLifeMetricsBenchmark --pattern acorn --generations 10000 --output acorn_full

# High-activity random soup
./VLifeMetricsBenchmark --pattern soup --soup-size 512 --soup-density 0.35 \
    --generations 1000 --output soup_512

# Verbose output for debugging
./VLifeMetricsBenchmark --pattern glider --generations 100 --verbose --interval 10
```

## Output File Formats

### CSV Format

The CSV file contains one row per generation with the following columns:

| Column | Type | Description |
|--------|------|-------------|
| `generation` | int | Generation number |
| `cells_born` | int | Cells that became alive |
| `cells_died` | int | Cells that died |
| `k` | int | Total state changes (born + died) |
| `total_live_cells` | int | Population (N) at generation end |
| `activity_ratio` | float | k/N ratio |
| `population_delta` | int | N_g - N_{g-1} |
| `peak_population` | int | Maximum N seen so far |
| `gens_since_change` | int | Generations since last k > 0 |
| `is_stable` | bool | 1 if stable for 10+ generations |
| `phase1_active_tiles` | int | Tiles processed in Phase 1 |
| `phase2_all_tiles` | int | Tiles processed in Phase 2 |
| `total_tiles` | int | Total tiles at generation end |
| `tiles_created` | int | New tiles this generation |
| `tiles_evicted` | int | Dead tiles removed |
| `min_tile_x` | int | Bounding box min X (tile coords) |
| `max_tile_x` | int | Bounding box max X |
| `min_tile_y` | int | Bounding box min Y |
| `max_tile_y` | int | Bounding box max Y |
| `extent_area` | int | Bounding box area in tiles |
| `spatial_density` | float | N / (extent_tiles × 1024) |
| `phase1_time_us` | int | Phase 1 time in microseconds |
| `phase2_time_us` | int | Phase 2 time in microseconds |
| `total_time_us` | int | Total generation time |
| `active_words` | int | Non-zero 64-bit words processed |
| `boundary_crossings` | int | Cross-tile neighbor updates |
| `atomic_operations` | int | CAS operations performed |

### JSON Format

```json
{
  "metadata": {
    "pattern": "acorn",
    "total_generations": 5000,
    "notes": "Warmup: 100 gens, Seed: 42"
  },
  "summary": {
    "total_generations": 5000,
    "total_k": 152847,
    "avg_activity_ratio": 0.0823,
    "max_activity_ratio": 1.39,
    "peak_population": 1057,
    "peak_tiles": 42,
    "avg_boundary_crossings_per_gen": 31.2,
    "avg_atomic_ops_per_gen": 89.4
  },
  "generations": [
    {
      "generation": 100,
      "cells_born": 45,
      "cells_died": 38,
      "k": 83,
      "total_live_cells": 312,
      "activity_ratio": 0.266,
      "population_delta": 7,
      "peak_population": 312,
      "is_stable": false,
      "tiles": {
        "phase1_active": 8,
        "phase2_all": 8,
        "total": 8,
        "created": 0,
        "evicted": 0
      },
      "extent": {
        "min_tile_x": 2,
        "max_tile_x": 5,
        "min_tile_y": 2,
        "max_tile_y": 5,
        "area": 16,
        "spatial_density": 0.019
      },
      "timing_us": {
        "phase1": 12,
        "phase2": 18,
        "total": 31
      },
      "optimization_metrics": {
        "active_words": 127,
        "boundary_crossings": 24,
        "atomic_operations": 68
      }
    }
  ]
}
```

## Programmatic Usage

You can also use the metrics system directly in your own code:

```cpp
#include "vlife/VLife.h"
#include "vlife/VLifeMetrics.h"

int main() {
    VLife board;

    // Set up your pattern
    board.setCell(100, 100, GameOfLife::CellState::ALIVE);
    // ...

#ifdef VLIFE_METRICS_ENABLED
    MetricsCollector collector;
    board.setMetricsCollector(&collector);
#endif

    // Run simulation
    for (int i = 0; i < 1000; i++) {
        board.runGeneration();
    }

#ifdef VLIFE_METRICS_ENABLED
    // Export results
    collector.exportCSV("my_simulation.csv");
    collector.exportJSON("my_simulation.json", "my_pattern", "test run");

    // Or access metrics programmatically
    auto summary = collector.getSummary();
    std::cout << "Average activity ratio: " << summary.avgActivityRatio << "\n";

    // Access per-generation data
    for (const auto& m : collector.getMetrics()) {
        std::cout << "Gen " << m.generation << ": k=" << m.getK() << "\n";
    }
#endif

    return 0;
}
```

## Analysis Examples

### Python Analysis

```python
import pandas as pd
import matplotlib.pyplot as plt

# Load metrics
df = pd.read_csv('acorn_metrics.csv')

# Verify activity-proportional complexity claim
# Cost should be O(k), not O(N)
plt.figure(figsize=(12, 4))

plt.subplot(1, 3, 1)
plt.scatter(df['k'], df['total_time_us'], alpha=0.5, s=10)
plt.xlabel('State Changes (k)')
plt.ylabel('Time (μs)')
plt.title('Time vs k (should be linear)')

plt.subplot(1, 3, 2)
plt.scatter(df['total_live_cells'], df['total_time_us'], alpha=0.5, s=10)
plt.xlabel('Population (N)')
plt.ylabel('Time (μs)')
plt.title('Time vs N')

plt.subplot(1, 3, 3)
plt.hist(df['activity_ratio'], bins=50)
plt.xlabel('Activity Ratio (k/N)')
plt.ylabel('Count')
plt.title('Activity Ratio Distribution')

plt.tight_layout()
plt.savefig('activity_analysis.png')

# Summary statistics
print(f"Average activity ratio: {df['activity_ratio'].mean():.4f}")
print(f"Median activity ratio: {df['activity_ratio'].median():.4f}")
print(f"Activity ratio range: {df['activity_ratio'].min():.4f} - {df['activity_ratio'].max():.4f}")

# Boundary cell percentage
total_updates = df['boundary_crossings'].sum() + df['k'].sum() * 4  # Rough estimate
boundary_pct = df['boundary_crossings'].sum() / total_updates * 100
print(f"Boundary operations: ~{boundary_pct:.1f}%")
```

### R Analysis

```r
library(ggplot2)
library(dplyr)

df <- read.csv("acorn_metrics.csv")

# Activity ratio over time
ggplot(df, aes(x = generation, y = activity_ratio)) +
  geom_line() +
  labs(title = "Activity Ratio Over Time",
       x = "Generation", y = "k/N")

# Correlation between k and time
cor_k_time <- cor(df$k, df$total_time_us)
cat(sprintf("Correlation (k, time): %.4f\n", cor_k_time))

# Linear regression: time ~ k
model <- lm(total_time_us ~ k, data = df)
summary(model)
```

## Metrics Explained

### Primary Metrics

- **k (state changes)**: The number of cells that change state (born + died). This is the key metric for proving O(k) complexity.

- **N (population)**: Total live cells. Traditional implementations are O(N).

- **Activity ratio (k/N)**: Typically 1-10% for most patterns after stabilization. This ratio demonstrates that k << N for realistic patterns.

### Tile Metrics

- **Phase 1 active tiles**: Tiles with `activityMask != 0` that were processed during rule evaluation. Tiles with no activity are skipped entirely.

- **Boundary crossings**: Cross-tile neighbor count updates. The paper claims ~12% of cells are on boundaries.

- **Atomic operations**: Compare-and-swap operations for thread-safe boundary updates.

### Timing Metrics

- **Phase 1 time**: Rule evaluation using LUT lookups. O(active_words) complexity.

- **Phase 2 time**: Applying state changes and updating neighbor counts. O(k) complexity.

## Architecture Notes

### Zero-Overhead Design

When `VLIFE_METRICS_ENABLED` is not defined:

```cpp
#define VLIFE_METRICS_INC_BORN(n) ((void)0)
#define VLIFE_METRICS_INC_DIED(n) ((void)0)
// ... all macros expand to no-ops
```

This means no branches, no memory accesses, and no function calls in the hot path.

### Thread-Local Counters

During parallel processing, each TBB worker thread accumulates metrics in thread-local counters:

```cpp
thread_local ThreadLocalCounters MetricsCollector::tlCounters;
```

Counters are merged at phase boundaries (2 merge points per generation) to minimize synchronization overhead.

### Data Flow

```
runGeneration() start
  → beginGeneration(gen, tileCount)

Phase 1 (parallel)
  → Each tile: ADD_ACTIVE_WORDS(popcount(activityMask))
  → mergeThreadLocalCounters()
  → endPhase1(activeTiles, time)

Phase 2 (parallel)
  → Each cell change: INC_BORN/INC_DIED
  → Each boundary op: INC_BOUNDARY
  → Each atomic: INC_ATOMIC
  → mergeThreadLocalCounters()
  → endPhase2(allTiles, time)

runGeneration() end
  → endGeneration(liveCount, extent, ...)
  → Store GenerationMetrics snapshot
```

## Verification

### Correctness Test

Run the same pattern with and without metrics, verify identical final states:

```bash
# Build both versions
cmake -DENABLE_METRICS=ON -B build-metrics ..
cmake -B build-release ..

# Run same pattern
cd build-metrics && ./VLifeMetricsBenchmark --pattern acorn --generations 1000
cd build-release && ./VLifeBenchmark  # Compare final tile count
```

### Performance Test

Compare benchmark results with and without metrics:

```bash
# Without metrics (release build)
./VLifeBenchmark --quick

# With metrics
./VLifeMetricsBenchmark --pattern soup --generations 1000
```

The non-metrics build should show identical performance to baseline since all instrumentation compiles away.

## Combining with Hardware Counter Profiling

For comprehensive analysis, use VLife metrics and hardware counter profiling in **separate runs**:

**Important:** `ENABLE_METRICS` adds per-generation overhead (thread-local counter updates, timing calls) that will affect CPU counter measurements. For accurate profiling, run these separately:

```bash
# Step 1: Collect activity metrics (with ENABLE_METRICS)
cmake -DENABLE_METRICS=ON -DENABLE_KPERF_COUNTERS=OFF ..
make VLifeMetricsBenchmark
./VLifeMetricsBenchmark --pattern acorn --generations 5000 --output acorn_activity

# Step 2: Collect CPU counter metrics (without ENABLE_METRICS overhead)
cmake -DENABLE_METRICS=OFF -DENABLE_KPERF_COUNTERS=ON ..
make VLifeCpuBenchmark
sudo ./VLifeCpuBenchmark --pattern acorn --generations 5000 --output acorn_cpu
```

Then cross-reference the two CSV files by generation number.

### Cross-Referencing Metrics

| VLife Metric | CPU Counter | Correlation |
|--------------|-------------|-------------|
| `phase1_time_us` | `duration_ns` (phase 1) | Direct timing |
| `phase2_time_us` | `l1d_misses` | High misses → slow phase 2 |
| `boundary_crossings` | `l1d_misses` | Boundary ops cause cache misses |
| `atomic_operations` | `cycles` | CAS contention increases cycles |
| High `k` (state changes) | High `instructions` | More work per generation |

### Example: Identifying Cache Bottlenecks

1. Find generations with slow `phase2_time_us` in VLife metrics
2. Check corresponding `l1d_misses` in CPU counter data
3. If correlated: optimize memory access patterns
4. If not correlated: look at branch mispredictions or instruction throughput

See [PERFORMANCE-OPTIMIZATIONS.md](PERFORMANCE-OPTIMIZATIONS.md) Section 11 for complete CPU counter documentation.

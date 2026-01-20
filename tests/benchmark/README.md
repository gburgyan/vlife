# VLife Benchmark Suite

This directory contains comprehensive benchmarks for evaluating VLife's performance characteristics.

## Quick Start

```bash
# Build everything
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)

# Run standard benchmark suite
make run_benchmark

# Run quick validation
make run_benchmark_quick

# Run extended publication benchmarks
make run_extended_benchmark
```

## Benchmark Types

### Standard Benchmarks (`VLifeBenchmark`)

The standard benchmark suite (`tests/benchmark/VLifeBenchmark.cpp`) tests:

1. **Glider Guns** - Sustained activity, tests steady-state performance
2. **Random Soup** - High activity, stresses all code paths
3. **Spaceship Fleet** - Moving patterns, tests tile boundary handling
4. **Block Grid** - Minimal changes, tests overhead on static patterns
5. **Gliders** - Tests tile boundary crossing
6. **Single Glider** - Long-running minimal pattern (50k generations)
7. **Acorn** - Methuselah pattern, chaotic growth then stabilization
8. **Parallel vs Sequential** - Measures parallel speedup

### Extended Benchmarks (`VLifeExtendedBenchmark`)

The extended suite (`tests/benchmark/ExtendedBenchmark.cpp`) provides publication-quality measurements:

| Benchmark | Purpose | Key Metrics |
|-----------|---------|-------------|
| Density Sweep | Performance vs population density | CUpS, gen/sec |
| Activity Test | Demonstrates O(k) vs O(N) scaling | Activity ratio correlation |
| Parallel Scaling | Strong scaling analysis | Speedup, efficiency |
| Methuselah Evolution | Performance through chaos | Growth vs stable phases |
| Oscillator Test | Periodic activity patterns | Predictability |
| Boundary Crossing | Tile boundary overhead | Aligned vs diagonal |

Run individual extended benchmarks:
```bash
make run_density_sweep     # Density sweep (0.1% to 50%)
make run_activity_test     # Activity-proportionality demonstration
```

Or with command-line options:
```bash
./VLifeExtendedBenchmark --density
./VLifeExtendedBenchmark --activity
./VLifeExtendedBenchmark --parallel
./VLifeExtendedBenchmark --methuselah
./VLifeExtendedBenchmark --oscillator
./VLifeExtendedBenchmark --boundary
```

## Reproducing Published Results

To reproduce the benchmark results in the technical paper:

### Hardware Requirements
- Apple Silicon Mac (M1/M2/M3) for ARM64 NEON results
- Or x86-64 with AVX2/AVX-512 for x86 results
- At least 8GB RAM
- Minimal background processes for consistent timing

### Steps

1. **Build in Release mode**:
   ```bash
   mkdir -p build && cd build
   cmake -DCMAKE_BUILD_TYPE=Release ..
   make -j$(nproc)
   ```

2. **Run warmup** (optional, helps thermal stability):
   ```bash
   ./VLifeBenchmark --quick
   ```

3. **Run full benchmark suite**:
   ```bash
   ./VLifeBenchmark 2>&1 | tee benchmark_results.txt
   ```

4. **Run extended benchmarks**:
   ```bash
   ./VLifeExtendedBenchmark 2>&1 | tee extended_results.txt
   ```

### Expected Variability

Results may vary by ±10% depending on:
- Thermal throttling
- Background system processes
- Memory pressure
- Power management settings

For publication-quality results:
- Run multiple trials (3-5 runs)
- Report mean ± standard deviation
- Close unnecessary applications
- Ensure device is plugged in (for laptops)

## Pattern Files

### Included Patterns

Standard patterns are defined in `BenchmarkPatterns.h`:
- `setupGliderGun()` - Gosper Glider Gun
- `setupRandomSoup()` - Configurable random pattern
- `setupSpaceships()` - LWSS fleet
- `setupBlockGrid()` - Still life grid
- `setupGlider()` / `setupGliders()` - Glider patterns
- `setupAcorn()` - Acorn methuselah

Extended patterns in `ExtendedPatterns.h`:
- `setupDensitySoup()` - Random soup with configurable density
- `setupBlock()` / `setupBeehive()` / `setupLoaf()` - Still lifes
- `setupBlinker()` / `setupToad()` / `setupBeacon()` - Period-2 oscillators
- `setupPulsar()` / `setupPentadecathlon()` - Longer period oscillators
- `setupRPentomino()` / `setupDiehard()` / `setupPiHeptomino()` - Methuselahs
- `setupMWSS()` / `setupHWSS()` - Larger spaceships

### Adding New Patterns

To add a custom pattern:

```cpp
// In your benchmark file or ExtendedPatterns.h
inline void setupMyPattern(VLife& board, int offsetX = 0, int offsetY = 0) {
    // Define your pattern cells
    board.setCell(offsetX + 0, offsetY + 0, GameOfLife::CellState::ALIVE);
    board.setCell(offsetX + 1, offsetY + 0, GameOfLife::CellState::ALIVE);
    // ... more cells
}
```

## Statistical Analysis

The benchmark infrastructure provides:

| Metric | Description |
|--------|-------------|
| `gen/sec` | Generations per second (higher = faster) |
| `μs/gen` | Microseconds per generation (lower = faster) |
| `min/max` | Timing range for variability assessment |
| `stddev` | Standard deviation of timing |
| `CV` | Coefficient of variation (stddev/mean) |
| `CUpS` | Cell Updates per Second (population × gen/sec) |

### Interpreting Results

**Activity-Proportional Scaling**: Compare gen/sec across patterns with different activity levels. Performance should correlate with changes per generation (k), not population (N).

**Parallel Efficiency**: Speedup / thread_count. Expect 50-80% efficiency at 8+ threads due to:
- Tile enumeration overhead
- Atomic operations at boundaries (~12% of cells)
- Memory bandwidth saturation

**Variability**: CV < 0.1 indicates consistent performance. Higher CV may indicate thermal throttling or system interference.

## Contributing

When adding new benchmarks:

1. Follow the existing pattern in `ExtendedBenchmark.cpp`
2. Include warmup phase (important for stable results)
3. Calculate and report statistical metrics
4. Document expected behavior and purpose
5. Use fixed random seeds for reproducibility

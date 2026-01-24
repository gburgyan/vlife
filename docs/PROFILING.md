# Profiling VLife

This guide covers how to profile VLife performance using the built-in benchmark profiling mode and macOS Instruments.

## Build Configurations

### Optimal Release Build (Recommended for Profiling)

```bash
cd build
cmake -DENABLE_AVX512=OFF -DENABLE_METRICS=OFF -DCMAKE_BUILD_TYPE=Release ..
make VLifeBenchmark
```

This produces the fastest build with:
- Native NEON SIMD on ARM64 (Apple Silicon)
- No metrics overhead
- Full `-O3` optimization

### Build with Debug Symbols (for Instruments)

```bash
cd build
cmake -DENABLE_AVX512=OFF -DENABLE_METRICS=OFF -DCMAKE_BUILD_TYPE=RelWithDebInfo ..
make VLifeBenchmark
```

This adds `-g` debug symbols while keeping `-O2` optimization, allowing Instruments to show source-level information.

## Profiling Mode

The benchmark includes a `--profile` mode designed for CPU profiling tools:

```bash
# Basic usage (30 seconds, glider pattern)
./VLifeBenchmark --profile

# Custom duration and pattern
./VLifeBenchmark --profile --duration 10 --profile-pattern acorn

# Show all options
./VLifeBenchmark --help
```

### Available Patterns

| Pattern | Description | Typical gen/sec |
|---------|-------------|-----------------|
| `glider` | Single glider (minimal, fast) | ~6M |
| `gliders` | 100 gliders | ~500K |
| `gun` | Single glider gun | ~2M |
| `guns` | 10 glider guns | ~300K |
| `lwss` | Single spaceship | ~4M |
| `spaceships` | 50 spaceships | ~400K |
| `soup` | 256x256 random soup | ~50K |
| `soup-large` | 512x512 random soup | ~15K |
| `blocks` | 64x64 static block grid | ~100K |
| `acorn` | Methuselah pattern | ~60K |

## macOS Profiling with `sample`

The `sample` command provides lightweight CPU profiling without needing Xcode Instruments:

```bash
# Start benchmark in background, then sample
./VLifeBenchmark --profile --duration 15 --profile-pattern glider &
sleep 1
sample $(pgrep -n VLifeBenchmark) 10 1 -file /tmp/profile.txt
wait

# View results
cat /tmp/profile.txt
```

### Reading Sample Output

The output includes:
- **Call graph**: Hierarchical view of where time is spent
- **Sort by top of stack**: Flat list of hot functions

Key functions to look for:
- `Tile::runGenerationPrepare()` - Phase 1: Conway rule evaluation
- `Tile::runGenerationChanges<false>()` - Phase 2: Apply state changes
- `Tile::applyVerticalDeltas()` - Cross-tile neighbor updates
- `Tile::markChangeCorners()` - Corner cell handling

## macOS Instruments

For more detailed profiling with GUI:

```bash
# Option 1: Launch via command line
xcrun xctrace record --template "Time Profiler" \
    --output profile.trace \
    --launch -- ./VLifeBenchmark --profile --duration 30

# Option 2: Run benchmark, attach from Instruments GUI
./VLifeBenchmark --profile --duration 60
# Then: Instruments.app -> Time Profiler -> Attach to Process
```

## Expected Performance Characteristics

### Single Glider (Optimal Build, Apple Silicon M-series)

| Metric | Expected |
|--------|----------|
| Performance | 6+ million gen/sec |
| Phase 1 (Prepare) | ~20% of time |
| Phase 2 (Changes) | ~60% of time |
| Clock/infrastructure | ~10% of time |
| Tile management | ~10% of time |

### Build Configuration Impact

| Configuration | Performance | Notes |
|---------------|-------------|-------|
| Release + NEON + no metrics | 6.0-6.5M gen/sec | Optimal |
| Release + NEON + metrics | 3.5-4.0M gen/sec | TLS overhead |
| Release + SIMDE (AVX512 emu) | 1.2-1.5M gen/sec | Emulation overhead |

## Troubleshooting

### Low Performance

1. Check build settings:
   ```bash
   grep -E "ENABLE_AVX512|ENABLE_METRICS|CMAKE_BUILD_TYPE" CMakeCache.txt
   ```

2. Ensure `ENABLE_AVX512=OFF` on ARM64 (native NEON is faster than SIMDE emulation)

3. Ensure `ENABLE_METRICS=OFF` for production profiling (metrics add ~40% overhead)

### Missing Symbols in Profile

Rebuild with `RelWithDebInfo`:
```bash
cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo ..
make VLifeBenchmark
```

### Clock Overhead Dominating Profile

If `mach_continuous_time` or `clock_gettime` appears high (>30%), the actual simulation is very fast. This is expected for simple patterns like single glider. Try a more complex pattern:
```bash
./VLifeBenchmark --profile --profile-pattern soup
```

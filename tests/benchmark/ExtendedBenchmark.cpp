//
// VLife Extended Benchmark Suite
// Comprehensive benchmarks for publication-quality evaluation
//

#include <chrono>
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <cmath>
#include <thread>
#include <fstream>
#include <algorithm>
#include <numeric>

#include "../../src/vlife/VLife.h"
#include "BenchmarkPatterns.h"
#include "ExtendedPatterns.h"

// ============================================================================
// BENCHMARK INFRASTRUCTURE
// ============================================================================

struct BenchmarkResult {
    std::string name;
    double genPerSec;
    double avgMicros;
    double minMicros;
    double maxMicros;
    double stdDevMicros;
    double coeffOfVariation;  // CV = stddev/mean
    size_t peakTiles;
    size_t finalTiles;
    size_t estimatedPopulation;
    size_t estimatedChangesPerGen;
    double cellUpdatesPerSec;  // CUpS = population * gen/sec
};

BenchmarkResult runBenchmarkWithStats(
    VLife& board,
    const std::string& name,
    int warmupGens,
    int measuredGens,
    size_t estPopulation = 0,
    size_t estChanges = 0
) {
    // Warmup phase
    for (int i = 0; i < warmupGens; i++) {
        board.runGeneration();
    }

    // Measurement phase
    std::vector<double> timings;
    timings.reserve(measuredGens);
    size_t peakTiles = 0;

    for (int i = 0; i < measuredGens; i++) {
        size_t tileCount = board.getTileCount();
        peakTiles = std::max(peakTiles, tileCount);

        auto start = std::chrono::high_resolution_clock::now();
        board.runGeneration();
        auto end = std::chrono::high_resolution_clock::now();

        double micros = std::chrono::duration<double, std::micro>(end - start).count();
        timings.push_back(micros);
    }

    // Calculate statistics
    BenchmarkResult result;
    result.name = name;
    result.peakTiles = peakTiles;
    result.finalTiles = board.getTileCount();
    result.estimatedPopulation = estPopulation;
    result.estimatedChangesPerGen = estChanges;

    // Mean
    double sum = std::accumulate(timings.begin(), timings.end(), 0.0);
    result.avgMicros = sum / timings.size();
    result.genPerSec = 1000000.0 / result.avgMicros;

    // Min/max
    result.minMicros = *std::min_element(timings.begin(), timings.end());
    result.maxMicros = *std::max_element(timings.begin(), timings.end());

    // Standard deviation
    double variance = 0;
    for (double t : timings) {
        double diff = t - result.avgMicros;
        variance += diff * diff;
    }
    result.stdDevMicros = std::sqrt(variance / timings.size());
    result.coeffOfVariation = result.stdDevMicros / result.avgMicros;

    // Cell updates per second
    if (estPopulation > 0) {
        result.cellUpdatesPerSec = estPopulation * result.genPerSec;
    } else {
        result.cellUpdatesPerSec = 0;
    }

    return result;
}

void printResult(const BenchmarkResult& r) {
    std::cout << std::fixed;
    std::cout << "\n" << r.name << ":\n";
    std::cout << std::setprecision(1);
    std::cout << "  Performance: " << r.genPerSec << " gen/sec ("
              << r.avgMicros << " μs/gen)\n";
    std::cout << "  Timing: min=" << r.minMicros << " μs, max=" << r.maxMicros
              << " μs, stddev=" << r.stdDevMicros << " μs\n";
    std::cout << std::setprecision(3);
    std::cout << "  Variability: CV=" << r.coeffOfVariation << "\n";
    std::cout << "  Tiles: peak=" << r.peakTiles << ", final=" << r.finalTiles << "\n";
    if (r.estimatedPopulation > 0) {
        std::cout << "  Est. population: ~" << r.estimatedPopulation << " cells\n";
        std::cout << std::setprecision(2);
        std::cout << "  Cell updates/sec: " << (r.cellUpdatesPerSec / 1e9) << " billion\n";
    }
}

std::string getArchitectureInfo() {
    std::string info;
#if defined(__x86_64__) || defined(_M_X64)
    info = "x86-64";
    #if defined(__AVX512F__)
        info += " (AVX-512)";
    #elif defined(__AVX2__)
        info += " (AVX2)";
    #elif defined(__AVX__)
        info += " (AVX)";
    #endif
#elif defined(__aarch64__) || defined(_M_ARM64)
    info = "ARM64";
    #if defined(__APPLE__)
        info += " (Apple Silicon)";
    #endif
    #if defined(__ARM_NEON)
        info += " NEON";
    #endif
#else
    info = "Unknown architecture";
#endif
    return info;
}

// ============================================================================
// BENCHMARK SUITES
// ============================================================================

void runDensitySweep() {
    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << "DENSITY SWEEP BENCHMARK\n";
    std::cout << "Fixed 256×256 grid, varying density (0.1% to 50%)\n";
    std::cout << std::string(60, '=') << "\n";

    std::vector<double> densities = {0.001, 0.01, 0.05, 0.10, 0.20, 0.30, 0.40, 0.50};
    std::vector<BenchmarkResult> results;

    for (double density : densities) {
        VLife board;
        ExtendedPatterns::setupDensitySoup(board, 256, 256, density);

        int warmup = 25;
        int measured = 200;
        size_t estPop = static_cast<size_t>(256 * 256 * density);
        size_t estChanges = static_cast<size_t>(estPop * 0.1); // ~10% activity typical

        std::ostringstream name;
        name << "Density " << std::fixed << std::setprecision(1) << (density * 100) << "%";

        auto result = runBenchmarkWithStats(board, name.str(), warmup, measured,
                                            estPop, estChanges);
        results.push_back(result);
        printResult(result);
    }

    // Summary table
    std::cout << "\n" << std::string(60, '-') << "\n";
    std::cout << "Density | Population | Gen/sec | CUpS (billions)\n";
    std::cout << std::string(60, '-') << "\n";
    for (const auto& r : results) {
        std::cout << std::fixed << std::setprecision(1);
        std::cout << std::setw(6) << (r.name.substr(8)) << " | "
                  << std::setw(10) << r.estimatedPopulation << " | "
                  << std::setw(7) << r.genPerSec << " | "
                  << std::setprecision(2) << (r.cellUpdatesPerSec / 1e9) << "\n";
    }
}

void runActivityProportionalityTest() {
    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << "ACTIVITY-PROPORTIONALITY DEMONSTRATION\n";
    std::cout << "Comparing patterns with different activity ratios\n";
    std::cout << std::string(60, '=') << "\n";

    std::vector<BenchmarkResult> results;

    // Single glider (minimal activity)
    {
        VLife board;
        BenchmarkPatterns::setupGlider(board, 0, 0);
        auto result = runBenchmarkWithStats(board, "Single glider",
                                            1000, 50000, 5, 1);
        results.push_back(result);
        printResult(result);
    }

    // 10 gliders
    {
        VLife board;
        BenchmarkPatterns::setupGliders(board, 10);
        auto result = runBenchmarkWithStats(board, "10 gliders",
                                            100, 5000, 50, 10);
        results.push_back(result);
        printResult(result);
    }

    // 100 gliders
    {
        VLife board;
        BenchmarkPatterns::setupGliders(board, 100);
        auto result = runBenchmarkWithStats(board, "100 gliders",
                                            100, 1000, 500, 100);
        results.push_back(result);
        printResult(result);
    }

    // Still life grid (zero activity after setup)
    {
        VLife board;
        BenchmarkPatterns::setupBlockGrid(board, 32);
        auto result = runBenchmarkWithStats(board, "Block grid 32×32",
                                            10, 1000, 4096, 0);
        results.push_back(result);
        printResult(result);
    }

    // Random soup (maximum activity)
    {
        VLife board;
        BenchmarkPatterns::setupRandomSoup(board, 256, 256, 0.3);
        auto result = runBenchmarkWithStats(board, "Random 256² 30%",
                                            25, 200, 19660, 2000);
        results.push_back(result);
        printResult(result);
    }

    // Summary
    std::cout << "\n" << std::string(60, '-') << "\n";
    std::cout << "Activity-Proportionality Summary:\n";
    std::cout << std::string(60, '-') << "\n";
    std::cout << "Pattern                | Est. k/gen | Gen/sec    | Ratio vs base\n";
    std::cout << std::string(60, '-') << "\n";
    double baseGenSec = results[0].genPerSec;
    for (const auto& r : results) {
        std::cout << std::left << std::setw(22) << r.name << " | "
                  << std::right << std::setw(10) << r.estimatedChangesPerGen << " | "
                  << std::fixed << std::setprecision(0) << std::setw(10) << r.genPerSec << " | "
                  << std::setprecision(1) << (baseGenSec / r.genPerSec) << "×\n";
    }
}

void runParallelScalingTest() {
    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << "PARALLEL SCALING ANALYSIS\n";
    std::cout << "Testing strong scaling on 1024×1024 random soup\n";
    std::cout << std::string(60, '=') << "\n";

    unsigned int maxThreads = std::thread::hardware_concurrency();
    std::cout << "Hardware threads available: " << maxThreads << "\n";

    // We can't easily control TBB thread count from here, so we compare
    // sequential vs parallel performance
    int size = 1024;
    int warmup = 10;
    int measured = 50;
    size_t estPop = static_cast<size_t>(size * size * 0.3);

    // Sequential
    {
        VLife board;
        BenchmarkPatterns::setupRandomSoup(board, size, size, 0.3);
        board.setParallelEnabled(false);

        auto result = runBenchmarkWithStats(board, "Sequential (1024²)",
                                            warmup, measured, estPop, estPop / 10);
        printResult(result);
    }

    // Parallel
    {
        VLife board;
        BenchmarkPatterns::setupRandomSoup(board, size, size, 0.3);
        board.setParallelEnabled(true);

        auto result = runBenchmarkWithStats(board, "Parallel (1024²)",
                                            warmup, measured, estPop, estPop / 10);
        printResult(result);
    }
}

void runMethuselahEvolution() {
    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << "METHUSELAH EVOLUTION ANALYSIS\n";
    std::cout << "Tracking performance through chaotic growth phase\n";
    std::cout << std::string(60, '=') << "\n";

    // R-pentomino evolution
    {
        VLife board;
        ExtendedPatterns::setupRPentomino(board, 500, 500);

        std::cout << "\nR-pentomino (stabilizes ~gen 1103):\n";
        std::cout << "Phase      | Generations | Gen/sec | Tiles\n";
        std::cout << std::string(50, '-') << "\n";

        // Growth phase (0-500)
        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < 500; i++) {
            board.runGeneration();
        }
        auto mid = std::chrono::high_resolution_clock::now();
        double growthMicros = std::chrono::duration<double, std::micro>(mid - start).count();
        double growthGenSec = 500.0 / (growthMicros / 1e6);
        std::cout << "Growth     | 0-500       | " << std::fixed << std::setprecision(0)
                  << growthGenSec << " | " << board.getTileCount() << "\n";

        // Stabilization phase (500-1500)
        start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < 1000; i++) {
            board.runGeneration();
        }
        mid = std::chrono::high_resolution_clock::now();
        double stableMicros = std::chrono::duration<double, std::micro>(mid - start).count();
        double stableGenSec = 1000.0 / (stableMicros / 1e6);
        std::cout << "Stable     | 500-1500    | " << std::fixed << std::setprecision(0)
                  << stableGenSec << " | " << board.getTileCount() << "\n";
    }

    // Acorn evolution
    {
        VLife board;
        BenchmarkPatterns::setupAcorn(board, 500, 500);

        std::cout << "\nAcorn (stabilizes ~gen 5206):\n";
        std::cout << "Phase      | Generations | Gen/sec | Tiles\n";
        std::cout << std::string(50, '-') << "\n";

        // Growth phase (0-2000)
        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < 2000; i++) {
            board.runGeneration();
        }
        auto mid = std::chrono::high_resolution_clock::now();
        double growthMicros = std::chrono::duration<double, std::micro>(mid - start).count();
        double growthGenSec = 2000.0 / (growthMicros / 1e6);
        std::cout << "Growth     | 0-2000      | " << std::fixed << std::setprecision(0)
                  << growthGenSec << " | " << board.getTileCount() << "\n";

        // Post-stabilization (5500-7500)
        for (int i = 0; i < 3500; i++) {
            board.runGeneration();
        }
        start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < 2000; i++) {
            board.runGeneration();
        }
        mid = std::chrono::high_resolution_clock::now();
        double stableMicros = std::chrono::duration<double, std::micro>(mid - start).count();
        double stableGenSec = 2000.0 / (stableMicros / 1e6);
        std::cout << "Stable     | 5500-7500   | " << std::fixed << std::setprecision(0)
                  << stableGenSec << " | " << board.getTileCount() << "\n";
    }
}

void runOscillatorTest() {
    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << "OSCILLATOR BENCHMARK\n";
    std::cout << "Testing periodic patterns with predictable activity\n";
    std::cout << std::string(60, '=') << "\n";

    // Blinker grid
    {
        VLife board;
        ExtendedPatterns::setupBlinkerGrid(board, 32, 6);  // 32×32 = 1024 blinkers
        auto result = runBenchmarkWithStats(board, "Blinker grid 32×32",
                                            50, 1000, 3072, 3072); // Each blinker: 3 cells, all change
        printResult(result);
    }

    // Pulsar (larger, period-3)
    {
        VLife board;
        for (int i = 0; i < 25; i++) {
            ExtendedPatterns::setupPulsar(board, (i % 5) * 20, (i / 5) * 20);
        }
        auto result = runBenchmarkWithStats(board, "25 Pulsars",
                                            50, 500, 1200, 400);
        printResult(result);
    }
}

void runBoundaryCrossingTest() {
    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << "BOUNDARY CROSSING TEST\n";
    std::cout << "Testing tile boundary overhead with diagonal gliders\n";
    std::cout << std::string(60, '=') << "\n";

    // Regular aligned gliders
    {
        VLife board;
        // Place gliders aligned to tile boundaries
        for (int i = 0; i < 100; i++) {
            int ox = (i % 10) * 32;  // Aligned to 32
            int oy = (i / 10) * 32;
            board.setCell(ox + 1, oy, GameOfLife::CellState::ALIVE);
            board.setCell(ox + 2, oy + 1, GameOfLife::CellState::ALIVE);
            board.setCell(ox, oy + 2, GameOfLife::CellState::ALIVE);
            board.setCell(ox + 1, oy + 2, GameOfLife::CellState::ALIVE);
            board.setCell(ox + 2, oy + 2, GameOfLife::CellState::ALIVE);
        }
        auto result = runBenchmarkWithStats(board, "100 aligned gliders",
                                            100, 1000, 500, 100);
        printResult(result);
    }

    // Misaligned diagonal gliders (maximum boundary crossing)
    {
        VLife board;
        ExtendedPatterns::setupBoundaryCrossingPattern(board, 100);
        auto result = runBenchmarkWithStats(board, "100 diagonal gliders",
                                            100, 1000, 500, 100);
        printResult(result);
    }
}

// ============================================================================
// MAIN
// ============================================================================

int main(int argc, char* argv[]) {
    std::cout << "VLife Extended Benchmark Suite\n";
    std::cout << "===============================\n";
    std::cout << "Architecture: " << getArchitectureInfo() << "\n";
    std::cout << "Hardware threads: " << std::thread::hardware_concurrency() << "\n";

    bool runAll = true;
    bool runDensity = false;
    bool runActivity = false;
    bool runParallel = false;
    bool runMethuselah = false;
    bool runOscillator = false;
    bool runBoundary = false;

    // Parse arguments
    for (int i = 1; i < argc; i++) {
        std::string arg(argv[i]);
        if (arg == "--density") { runDensity = true; runAll = false; }
        else if (arg == "--activity") { runActivity = true; runAll = false; }
        else if (arg == "--parallel") { runParallel = true; runAll = false; }
        else if (arg == "--methuselah") { runMethuselah = true; runAll = false; }
        else if (arg == "--oscillator") { runOscillator = true; runAll = false; }
        else if (arg == "--boundary") { runBoundary = true; runAll = false; }
        else if (arg == "--help" || arg == "-h") {
            std::cout << "\nUsage: " << argv[0] << " [options]\n";
            std::cout << "Options:\n";
            std::cout << "  --density     Run density sweep benchmark\n";
            std::cout << "  --activity    Run activity-proportionality test\n";
            std::cout << "  --parallel    Run parallel scaling test\n";
            std::cout << "  --methuselah  Run methuselah evolution analysis\n";
            std::cout << "  --oscillator  Run oscillator benchmark\n";
            std::cout << "  --boundary    Run boundary crossing test\n";
            std::cout << "  (no options)  Run all benchmarks\n";
            return 0;
        }
    }

    if (runAll || runDensity) runDensitySweep();
    if (runAll || runActivity) runActivityProportionalityTest();
    if (runAll || runParallel) runParallelScalingTest();
    if (runAll || runMethuselah) runMethuselahEvolution();
    if (runAll || runOscillator) runOscillatorTest();
    if (runAll || runBoundary) runBoundaryCrossingTest();

    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << "Extended benchmark suite complete.\n";
    std::cout << std::string(60, '=') << "\n";

    return 0;
}

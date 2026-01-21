//
// VLife Metrics Benchmark
//
// Standalone tool for running VLife simulations with detailed metrics collection.
// Produces CSV and JSON output for analysis and research paper data.
//
// Usage:
//   ./VLifeMetricsBenchmark --pattern <name> --generations <n> --output <filename>
//
// Requires building with -DENABLE_METRICS=ON
//

#include <chrono>
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <cstring>

#include "../../src/vlife/VLife.h"
#include "../../src/vlife/VLifeMetrics.h"
#include "BenchmarkPatterns.h"

#ifndef VLIFE_METRICS_ENABLED
#error "MetricsBenchmark requires ENABLE_METRICS=ON. Rebuild with: cmake -DENABLE_METRICS=ON .."
#endif

struct Config {
    std::string pattern = "acorn";
    int generations = 5000;
    int warmup = 100;
    std::string outputBase = "vlife_metrics";
    bool exportCSV = true;
    bool exportJSON = true;
    bool verbose = false;
    int reportInterval = 500;  // Print progress every N generations
    uint32_t seed = 42;
    int soupSize = 256;
    double soupDensity = 0.3;
};

void printUsage(const char* progName) {
    std::cout << "VLife Metrics Benchmark\n";
    std::cout << "=======================\n\n";
    std::cout << "Usage: " << progName << " [options]\n\n";
    std::cout << "Options:\n";
    std::cout << "  --pattern <name>      Pattern to simulate (default: acorn)\n";
    std::cout << "                        Available: acorn, glider, gliders, guns, soup, blocks\n";
    std::cout << "  --generations <n>     Number of generations to run (default: 5000)\n";
    std::cout << "  --warmup <n>          Warmup generations before collecting metrics (default: 100)\n";
    std::cout << "  --output <name>       Output filename base (default: vlife_metrics)\n";
    std::cout << "  --csv-only            Only export CSV (no JSON)\n";
    std::cout << "  --json-only           Only export JSON (no CSV)\n";
    std::cout << "  --verbose             Print detailed progress\n";
    std::cout << "  --interval <n>        Progress report interval (default: 500)\n";
    std::cout << "  --seed <n>            Random seed for soup pattern (default: 42)\n";
    std::cout << "  --soup-size <n>       Size of random soup (default: 256)\n";
    std::cout << "  --soup-density <f>    Density of random soup 0.0-1.0 (default: 0.3)\n";
    std::cout << "  --help                Show this help message\n";
    std::cout << "\nExamples:\n";
    std::cout << "  " << progName << " --pattern acorn --generations 5000 --output acorn_data\n";
    std::cout << "  " << progName << " --pattern soup --soup-size 512 --generations 1000\n";
}

Config parseArgs(int argc, char* argv[]) {
    Config config;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printUsage(argv[0]);
            exit(0);
        } else if (strcmp(argv[i], "--pattern") == 0 && i + 1 < argc) {
            config.pattern = argv[++i];
        } else if (strcmp(argv[i], "--generations") == 0 && i + 1 < argc) {
            config.generations = std::stoi(argv[++i]);
        } else if (strcmp(argv[i], "--warmup") == 0 && i + 1 < argc) {
            config.warmup = std::stoi(argv[++i]);
        } else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
            config.outputBase = argv[++i];
        } else if (strcmp(argv[i], "--csv-only") == 0) {
            config.exportJSON = false;
        } else if (strcmp(argv[i], "--json-only") == 0) {
            config.exportCSV = false;
        } else if (strcmp(argv[i], "--verbose") == 0) {
            config.verbose = true;
        } else if (strcmp(argv[i], "--interval") == 0 && i + 1 < argc) {
            config.reportInterval = std::stoi(argv[++i]);
        } else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            config.seed = std::stoul(argv[++i]);
        } else if (strcmp(argv[i], "--soup-size") == 0 && i + 1 < argc) {
            config.soupSize = std::stoi(argv[++i]);
        } else if (strcmp(argv[i], "--soup-density") == 0 && i + 1 < argc) {
            config.soupDensity = std::stod(argv[++i]);
        }
    }

    return config;
}

void setupPattern(VLife& board, const Config& config) {
    if (config.pattern == "acorn") {
        BenchmarkPatterns::setupAcorn(board, 100, 100);
    } else if (config.pattern == "glider") {
        BenchmarkPatterns::setupGlider(board, 0, 0);
    } else if (config.pattern == "gliders") {
        BenchmarkPatterns::setupGliders(board, 100);
    } else if (config.pattern == "guns" || config.pattern == "gun") {
        BenchmarkPatterns::setupGliderGuns(board, 10);
    } else if (config.pattern == "soup") {
        BenchmarkPatterns::setupRandomSoup(board, config.soupSize, config.soupSize,
                                            config.soupDensity, config.seed);
    } else if (config.pattern == "blocks") {
        BenchmarkPatterns::setupBlockGrid(board, 64);
    } else if (config.pattern == "spaceships") {
        BenchmarkPatterns::setupSpaceships(board, 50);
    } else if (config.pattern == "r-pentomino") {
        // R-pentomino methuselah
        // .XX
        // XX.
        // .X.
        board.setCell(101, 100, GameOfLife::CellState::ALIVE);
        board.setCell(102, 100, GameOfLife::CellState::ALIVE);
        board.setCell(100, 101, GameOfLife::CellState::ALIVE);
        board.setCell(101, 101, GameOfLife::CellState::ALIVE);
        board.setCell(101, 102, GameOfLife::CellState::ALIVE);
    } else {
        std::cerr << "Unknown pattern: " << config.pattern << "\n";
        std::cerr << "Available patterns: acorn, glider, gliders, guns, soup, blocks, spaceships, r-pentomino\n";
        exit(1);
    }
}

int main(int argc, char* argv[]) {
    Config config = parseArgs(argc, argv);

    std::cout << "VLife Metrics Benchmark\n";
    std::cout << "=======================\n";
    std::cout << "Pattern: " << config.pattern << "\n";
    std::cout << "Warmup: " << config.warmup << " generations\n";
    std::cout << "Measured: " << config.generations << " generations\n";
    std::cout << "Output: " << config.outputBase << ".csv/.json\n";
    std::cout << "\n";

    // Create board and set up pattern
    VLife board;
    setupPattern(board, config);

    // Create metrics collector
    MetricsCollector collector;
    board.setMetricsCollector(&collector);

    // Warmup phase (no metrics collection)
    std::cout << "Running warmup (" << config.warmup << " generations)...\n";
    board.setMetricsCollector(nullptr);  // Disable metrics during warmup
    for (int i = 0; i < config.warmup; i++) {
        board.runGeneration();
    }

    // Re-enable metrics collection
    collector.reset();
    board.setMetricsCollector(&collector);

    // Measurement phase
    std::cout << "Running measurement phase (" << config.generations << " generations)...\n";

    auto startTime = std::chrono::high_resolution_clock::now();
    uint64_t lastReportGen = 0;

    for (int gen = 0; gen < config.generations; gen++) {
        board.runGeneration();

        // Progress report
        if (config.verbose && (gen + 1) % config.reportInterval == 0) {
            const auto& m = collector.getCurrentMetrics();
            std::cout << "Gen " << std::setw(6) << (gen + 1)
                      << ": N=" << std::setw(8) << m.totalLiveCells
                      << ", k=" << std::setw(6) << m.getK()
                      << ", tiles=" << std::setw(4) << m.totalTiles
                      << ", ratio=" << std::fixed << std::setprecision(4) << m.getActivityRatio()
                      << "\n";
        } else if ((gen + 1) % config.reportInterval == 0) {
            std::cout << "  Progress: " << (gen + 1) << "/" << config.generations << "\n";
        }
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    double totalSeconds = std::chrono::duration<double>(endTime - startTime).count();

    // Print summary
    std::cout << "\n";
    std::cout << "Results\n";
    std::cout << "-------\n";
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Total time: " << totalSeconds << " seconds\n";
    std::cout << "Speed: " << (config.generations / totalSeconds) << " gen/sec\n";

    auto summary = collector.getSummary();
    std::cout << "\nMetrics Summary:\n";
    std::cout << "  Total generations: " << summary.totalGenerations << "\n";
    std::cout << "  Total state changes (sum of k): " << summary.totalK << "\n";
    std::cout << "  Average activity ratio (k/N): " << std::setprecision(6) << summary.avgActivityRatio << "\n";
    std::cout << "  Max activity ratio: " << summary.maxActivityRatio << "\n";
    std::cout << "  Peak population: " << summary.peakPopulation << "\n";
    std::cout << "  Peak tiles: " << summary.peakTiles << "\n";
    std::cout << "  Avg boundary crossings/gen: " << std::setprecision(1) << summary.avgBoundaryCrossingsPerGen << "\n";
    std::cout << "  Avg atomic ops/gen: " << summary.avgAtomicOpsPerGen << "\n";

    // Export results
    if (config.exportCSV) {
        std::string csvFile = config.outputBase + ".csv";
        collector.exportCSV(csvFile);
        std::cout << "\nExported CSV: " << csvFile << "\n";
    }

    if (config.exportJSON) {
        std::string jsonFile = config.outputBase + ".json";
        std::string notes = "Warmup: " + std::to_string(config.warmup) +
                            " gens, Seed: " + std::to_string(config.seed);
        collector.exportJSON(jsonFile, config.pattern, notes);
        std::cout << "Exported JSON: " << jsonFile << "\n";
    }

    std::cout << "\nBenchmark complete.\n";

    return 0;
}

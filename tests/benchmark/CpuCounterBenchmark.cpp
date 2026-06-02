//
// VLife CPU Counter Benchmark
//
// Standalone tool for profiling VLife with hardware performance counters
// on Apple Silicon. Outputs CSV data for analysis.
//
// Usage:
//   sudo ./VLifeCpuBenchmark --pattern <name> --generations <n> --output <filename>
//
// Requires:
//   - Building with -DENABLE_KPERF_COUNTERS=ON
//   - Running with sudo (or SIP disabled) for kperf access
//
// Output columns:
//   generation,cycles,instructions,l1d_misses,branch_mispred,ipc,miss_rate_per_1k,mispred_rate_per_1k,duration_ns,live_cells,tiles
//

#include <chrono>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <string>
#include <vector>
#include <cstring>

#include "../../src/vlife/VLife.h"
#include "../../src/vlife/AppleSiliconCounters.h"
#include "BenchmarkPatterns.h"

// Config structure
struct Config {
    std::string pattern = "acorn";
    int generations = 1000;
    int warmup = 50;
    std::string outputBase = "cpu_metrics";
    bool verbose = false;
    int reportInterval = 100;
    uint32_t seed = 42;
    int soupSize = 256;
    double soupDensity = 0.3;
    bool timingOnly = false;  // Fall back to timing if counters unavailable
    bool sequential = true;   // Use sequential mode for accurate profiling
};

void printUsage(const char* progName) {
    std::cout << "VLife CPU Counter Benchmark\n";
    std::cout << "===========================\n\n";
    std::cout << "Usage: sudo " << progName << " [options]\n\n";
    std::cout << "Options:\n";
    std::cout << "  --pattern <name>      Pattern to simulate (default: acorn)\n";
    std::cout << "                        Available: acorn, glider, gliders, guns, soup, blocks\n";
    std::cout << "  --generations <n>     Number of generations to run (default: 1000)\n";
    std::cout << "  --warmup <n>          Warmup generations (default: 50)\n";
    std::cout << "  --output <name>       Output filename base (default: cpu_metrics)\n";
    std::cout << "  --verbose             Print detailed progress\n";
    std::cout << "  --interval <n>        Progress report interval (default: 100)\n";
    std::cout << "  --seed <n>            Random seed for soup pattern (default: 42)\n";
    std::cout << "  --soup-size <n>       Size of random soup (default: 256)\n";
    std::cout << "  --soup-density <f>    Density of random soup 0.0-1.0 (default: 0.3)\n";
    std::cout << "  --timing-only         Use timing only (no hardware counters)\n";
    std::cout << "  --parallel            Enable parallel execution (default: sequential)\n";
    std::cout << "  --help                Show this help message\n";
    std::cout << "\nNotes:\n";
    std::cout << "  - Requires sudo for hardware counter access (kperf)\n";
    std::cout << "  - Build with: cmake -DENABLE_KPERF_COUNTERS=ON ..\n";
    std::cout << "  - Runs in single-threaded mode by default for accurate profiling\n";
    std::cout << "  - Use --parallel to profile multi-threaded execution\n";
    std::cout << "\nExamples:\n";
    std::cout << "  sudo " << progName << " --pattern acorn --generations 1000 --output acorn_cpu\n";
    std::cout << "  sudo " << progName << " --pattern soup --soup-size 512 --generations 500\n";
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
        } else if (strcmp(argv[i], "--timing-only") == 0) {
            config.timingOnly = true;
        } else if (strcmp(argv[i], "--parallel") == 0) {
            config.sequential = false;
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

// Run benchmark with hardware counters
void runBenchmarkWithCounters(VLife& board, int generations, std::ostream& csv,
                               const Config& config, CounterSession& session) {
    // CSV header
    csv << "generation,cycles,instructions,l1d_misses,branch_mispred,"
        << "ipc,miss_rate_per_1k,mispred_rate_per_1k,duration_ns,live_cells,tiles\n";

    for (int gen = 0; gen < generations; gen++) {
        auto startTime = std::chrono::high_resolution_clock::now();
        session.start();

        board.runGeneration();

        HardwareCounters counters = session.stop();
        auto endTime = std::chrono::high_resolution_clock::now();
        uint64_t duration = std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - startTime).count();

        uint64_t liveCells = board.getTotalLiveCells();
        size_t tileCount = board.getTileCount();

        // Write metrics
        csv << gen << ","
            << counters.cycles << ","
            << counters.instructions << ","
            << counters.l1d_cache_misses << ","
            << counters.branch_mispredictions << ","
            << std::fixed << std::setprecision(3) << counters.ipc() << ","
            << std::setprecision(4) << counters.l1d_miss_rate() << ","
            << counters.branch_mispred_rate() << ","
            << duration << ","
            << liveCells << ","
            << tileCount << "\n";

        // Progress report
        if (config.verbose && (gen + 1) % config.reportInterval == 0) {
            std::cout << "Gen " << std::setw(5) << (gen + 1)
                      << ": cells=" << std::setw(7) << liveCells
                      << ", tiles=" << std::setw(4) << tileCount
                      << ", IPC=" << std::fixed << std::setprecision(2) << counters.ipc()
                      << ", L1D miss/1k=" << std::setprecision(2) << counters.l1d_miss_rate()
                      << "\n";
        } else if ((gen + 1) % config.reportInterval == 0) {
            std::cout << "  Progress: " << (gen + 1) << "/" << generations << "\n";
        }
    }
}

// Run benchmark with timing only (fallback when counters unavailable)
void runBenchmarkTimingOnly(VLife& board, int generations, std::ostream& csv,
                             const Config& config) {
    // CSV header (zeros for counter columns)
    csv << "generation,cycles,instructions,l1d_misses,branch_mispred,"
        << "ipc,miss_rate_per_1k,mispred_rate_per_1k,duration_ns,live_cells,tiles\n";

    for (int gen = 0; gen < generations; gen++) {
        auto startTime = std::chrono::high_resolution_clock::now();

        board.runGeneration();

        auto endTime = std::chrono::high_resolution_clock::now();
        uint64_t duration = std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - startTime).count();

        uint64_t liveCells = board.getTotalLiveCells();
        size_t tileCount = board.getTileCount();

        // Write with zeros for counter columns
        csv << gen << ",0,0,0,0,0,0,0,"
            << duration << ","
            << liveCells << ","
            << tileCount << "\n";

        // Progress report
        if (config.verbose && (gen + 1) % config.reportInterval == 0) {
            std::cout << "Gen " << std::setw(5) << (gen + 1)
                      << ": cells=" << std::setw(7) << liveCells
                      << ", tiles=" << std::setw(4) << tileCount
                      << ", time=" << std::setw(8) << duration << "ns\n";
        } else if ((gen + 1) % config.reportInterval == 0) {
            std::cout << "  Progress: " << (gen + 1) << "/" << generations << "\n";
        }
    }
}

int main(int argc, char* argv[]) {
    Config config = parseArgs(argc, argv);

    std::cout << "VLife CPU Counter Benchmark\n";
    std::cout << "===========================\n";
    std::cout << "Pattern: " << config.pattern << "\n";
    std::cout << "Warmup: " << config.warmup << " generations\n";
    std::cout << "Measured: " << config.generations << " generations\n";
    std::cout << "Output: " << config.outputBase << ".csv\n";
    std::cout << "Mode: " << (config.sequential ? "sequential" : "parallel") << "\n";
    std::cout << "\n";

    // Check counter support
    bool useCounters = !config.timingOnly;
    CounterSession session;

    if (useCounters) {
        if (!CounterSession::isSupported()) {
            std::cout << "WARNING: kperf framework not available.\n";
            std::cout << "  Falling back to timing-only mode.\n\n";
            useCounters = false;
        } else if (!session.isValid()) {
            std::cout << "WARNING: Failed to initialize hardware counters.\n";
            std::cout << "  Error: " << session.getError() << "\n";
            std::cout << "  Try running with sudo.\n";
            std::cout << "  Falling back to timing-only mode.\n\n";
            useCounters = false;
        } else {
            std::cout << "Hardware counters: ENABLED\n";
            std::cout << "  Measuring: cycles, instructions, L1D misses, branch mispredictions\n\n";
        }
    } else {
        std::cout << "Mode: timing-only (--timing-only specified)\n\n";
    }

    // Warn if ENABLE_METRICS is also enabled (adds overhead to measurements)
#ifdef VLIFE_METRICS_ENABLED
    std::cout << "WARNING: Built with ENABLE_METRICS=ON. This adds overhead to each generation\n";
    std::cout << "  that will affect CPU counter measurements. For accurate profiling, rebuild with:\n";
    std::cout << "  cmake -DENABLE_KPERF_COUNTERS=ON -DENABLE_METRICS=OFF ..\n\n";
#endif

    // Create board and set up pattern
    VLife board;
    setupPattern(board, config);

    // Set execution mode
    board.setParallelEnabled(!config.sequential);

    std::cout << "Initial population: " << board.getTotalLiveCells() << " cells\n\n";

    // Warmup phase (runs normally, not profiled)
    std::cout << "Running warmup (" << config.warmup << " generations)...\n";
    for (int i = 0; i < config.warmup; i++) {
        board.runGeneration();
    }
    std::cout << "Warmup complete. Population: " << board.getTotalLiveCells() << "\n\n";

    // Open output file
    std::string csvFile = config.outputBase + ".csv";
    std::ofstream csv(csvFile);
    if (!csv.is_open()) {
        std::cerr << "Error: Could not open output file: " << csvFile << "\n";
        return 1;
    }

    // Run measurement phase
    std::cout << "Running measurement phase (" << config.generations << " generations)...\n";
    auto startTime = std::chrono::high_resolution_clock::now();

    if (useCounters) {
        runBenchmarkWithCounters(board, config.generations, csv, config, session);
    } else {
        runBenchmarkTimingOnly(board, config.generations, csv, config);
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    double totalSeconds = std::chrono::duration<double>(endTime - startTime).count();

    csv.close();

    // Print summary
    std::cout << "\n";
    std::cout << "Results\n";
    std::cout << "-------\n";
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Total time: " << totalSeconds << " seconds\n";
    std::cout << "Speed: " << (config.generations / totalSeconds) << " gen/sec\n";
    std::cout << "Final population: " << board.getTotalLiveCells() << " cells\n";
    std::cout << "Final tiles: " << board.getTileCount() << "\n";
    std::cout << "\nExported: " << csvFile << "\n";

    if (useCounters) {
        std::cout << "\nAnalysis tips:\n";
        std::cout << "  - IPC > 2.5 is good for compute-bound code\n";
        std::cout << "  - miss_rate_per_1k > 10 suggests cache pressure\n";
        std::cout << "  - mispred_rate_per_1k > 5 suggests branch prediction issues\n";
    }

    std::cout << "\nBenchmark complete.\n";
    return 0;
}

//
// VLife Benchmark Suite
//

#include <chrono>
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <cmath>
#include <thread>

#ifdef __APPLE__
#include <pthread.h>
#include <sys/qos.h>
#endif

#include "../../src/vlife/VLife.h"
#include "BenchmarkPatterns.h"

// Set thread to high-priority QoS on Apple Silicon to ensure P-core scheduling
// and aggressive DVFS. No-op on other platforms.
static void setHighPriorityQoS() {
#ifdef __APPLE__
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
}

// Prime the CPU by running a heavy VLife workload until it reaches peak frequency.
// Uses a large random soup pattern - this is representative of the actual benchmark
// instruction mix and memory access patterns.
static void primeCPU(int milliseconds = 500) {
    // Use a large random soup - representative of actual VLife workload
    VLife warmupBoard;
    BenchmarkPatterns::setupRandomSoup(warmupBoard, 512, 512, 0.3);

    auto start = std::chrono::high_resolution_clock::now();
    auto target = start + std::chrono::milliseconds(milliseconds);

    while (std::chrono::high_resolution_clock::now() < target) {
        warmupBoard.runGeneration();
    }
}

class VLifeBenchmark {
public:
    struct Result {
        double generationsPerSecond;
        double avgMicrosPerGeneration;
        double minMicrosPerGeneration;
        double maxMicrosPerGeneration;
        double stdDevMicros;
        size_t peakTileCount;
        size_t finalTileCount;
    };

    static Result runBenchmark(
        VLife& board,
        int warmupGenerations,
        int measuredGenerations
    ) {
        // Warmup phase
        for (int i = 0; i < warmupGenerations; i++) {
            board.runGeneration();
        }

        // Measurement phase
        std::vector<double> timings;
        timings.reserve(measuredGenerations);
        size_t peakTileCount = 0;

        for (int i = 0; i < measuredGenerations; i++) {
            size_t tileCount = countTiles(board);
            if (tileCount > peakTileCount) {
                peakTileCount = tileCount;
            }

            auto start = std::chrono::high_resolution_clock::now();
            board.runGeneration();
            auto end = std::chrono::high_resolution_clock::now();

            double micros = std::chrono::duration<double, std::micro>(end - start).count();
            timings.push_back(micros);
        }

        // Calculate statistics
        Result result{};
        result.peakTileCount = peakTileCount;
        result.finalTileCount = countTiles(board);

        double sum = 0;
        result.minMicrosPerGeneration = timings[0];
        result.maxMicrosPerGeneration = timings[0];

        for (double t : timings) {
            sum += t;
            if (t < result.minMicrosPerGeneration) result.minMicrosPerGeneration = t;
            if (t > result.maxMicrosPerGeneration) result.maxMicrosPerGeneration = t;
        }

        result.avgMicrosPerGeneration = sum / timings.size();
        result.generationsPerSecond = 1000000.0 / result.avgMicrosPerGeneration;

        // Calculate standard deviation
        double variance = 0;
        for (double t : timings) {
            double diff = t - result.avgMicrosPerGeneration;
            variance += diff * diff;
        }
        result.stdDevMicros = std::sqrt(variance / timings.size());

        return result;
    }

    static std::string getArchitectureInfo() {
        std::string info;

#if defined(__x86_64__) || defined(_M_X64)
        info = "x86-64";
    #if defined(__AVX512F__)
        info += " (AVX-512)";
    #elif defined(__AVX2__)
        info += " (AVX2)";
    #elif defined(__AVX__)
        info += " (AVX)";
    #elif defined(__SSE4_2__)
        info += " (SSE4.2)";
    #endif
    #if defined(__BMI2__)
        info += " +BMI2";
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

private:
    static size_t countTiles(VLife& board) {
        return board.getTileCount();
    }
};

void printResult(const std::string& patternName, const VLifeBenchmark::Result& result,
                 int warmup, int measured) {
    std::cout << "\nPattern: " << patternName << "\n";
    std::cout << "  Warmup: " << warmup << " generations\n";
    std::cout << "  Measured: " << measured << " generations\n";
    std::cout << std::fixed << std::setprecision(1);
    std::cout << "  Result: " << result.generationsPerSecond << " gen/sec ("
              << result.avgMicrosPerGeneration << " us/gen)\n";
    std::cout << "  Timing: min=" << result.minMicrosPerGeneration
              << " us, max=" << result.maxMicrosPerGeneration
              << " us, stddev=" << result.stdDevMicros << " us\n";
    std::cout << "  Tiles: peak=" << result.peakTileCount
              << ", final=" << result.finalTileCount << "\n";
}

// Setup a pattern by name for profiling mode
void setupPatternByName(VLife& board, const std::string& patternName) {
    if (patternName == "glider") {
        BenchmarkPatterns::setupGlider(board, 0, 0);
    } else if (patternName == "gliders") {
        BenchmarkPatterns::setupGliders(board, 100);
    } else if (patternName == "gun") {
        BenchmarkPatterns::setupGliderGun(board, 0, 0);
    } else if (patternName == "guns") {
        BenchmarkPatterns::setupGliderGuns(board, 10);
    } else if (patternName == "lwss") {
        BenchmarkPatterns::setupLWSS(board, 0, 0);
    } else if (patternName == "spaceships") {
        BenchmarkPatterns::setupSpaceships(board, 50);
    } else if (patternName == "soup") {
        BenchmarkPatterns::setupRandomSoup(board, 256, 256, 0.3);
    } else if (patternName == "soup-large") {
        BenchmarkPatterns::setupRandomSoup(board, 512, 512, 0.3);
    } else if (patternName == "blocks") {
        BenchmarkPatterns::setupBlockGrid(board, 64);
    } else if (patternName == "acorn") {
        BenchmarkPatterns::setupAcorn(board);
    } else {
        std::cerr << "Unknown pattern: " << patternName << "\n";
        std::cerr << "Available patterns: glider, gliders, gun, guns, lwss, spaceships, soup, soup-large, blocks, acorn\n";
        exit(1);
    }
}

// Run profiling mode - runs for a specified duration for Instruments Time Profiler sampling
void runProfilingMode(int durationSeconds, const std::string& patternName) {
    std::cout << "Profiling mode: running '" << patternName << "' for " << durationSeconds << " seconds...\n";
    std::cout << "Attach Instruments Time Profiler now if needed.\n";
    std::cout << std::flush;

    VLife board;
    setupPatternByName(board, patternName);

    auto startTime = std::chrono::high_resolution_clock::now();
    auto endTime = startTime + std::chrono::seconds(durationSeconds);
    uint64_t generations = 0;

    while (std::chrono::high_resolution_clock::now() < endTime) {
        board.runGeneration();
        generations++;
    }

    auto actualEnd = std::chrono::high_resolution_clock::now();
    double elapsedSeconds = std::chrono::duration<double>(actualEnd - startTime).count();
    double genPerSec = generations / elapsedSeconds;

    std::cout << std::fixed << std::setprecision(1);
    std::cout << "Completed " << generations << " generations in " << elapsedSeconds << "s ";
    std::cout << "(" << std::setprecision(0) << genPerSec << " gen/sec)\n";
    std::cout << "Final tile count: " << board.getTileCount() << "\n";
}

int main(int argc, char* argv[]) {
    // Set high-priority QoS immediately for P-core scheduling on Apple Silicon
    setHighPriorityQoS();

    // Parse command line arguments
    bool quickMode = false;
    bool skipPrime = false;
    bool profileMode = false;
    int profileDuration = 30;
    std::string profilePattern = "glider";

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--quick" || arg == "-q") {
            quickMode = true;
        } else if (arg == "--no-prime") {
            skipPrime = true;
        } else if (arg == "--profile") {
            profileMode = true;
        } else if (arg == "--duration" && i + 1 < argc) {
            profileDuration = std::stoi(argv[++i]);
        } else if (arg == "--profile-pattern" && i + 1 < argc) {
            profilePattern = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: VLifeBenchmark [options]\n";
            std::cout << "Options:\n";
            std::cout << "  --quick, -q          Quick mode (reduced iterations)\n";
            std::cout << "  --no-prime           Skip CPU priming\n";
            std::cout << "  --profile            Enable profiling mode (runs for duration)\n";
            std::cout << "  --duration <sec>     Profiling duration in seconds (default: 30)\n";
            std::cout << "  --profile-pattern <name>  Pattern to profile (default: glider)\n";
            std::cout << "    Patterns: glider, gliders, gun, guns, lwss, spaceships,\n";
            std::cout << "              soup, soup-large, blocks, acorn\n";
            std::cout << "  --help, -h           Show this help\n";
            return 0;
        }
    }

    // Handle profiling mode separately
    if (profileMode) {
        std::cout << "VLife Profiling Mode\n";
        std::cout << "====================\n";
        std::cout << "Architecture: " << VLifeBenchmark::getArchitectureInfo() << "\n";
        std::cout << "\nBuild with debug symbols for Instruments:\n";
        std::cout << "  cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo ..\n\n";
        runProfilingMode(profileDuration, profilePattern);
        return 0;
    }

    std::cout << "VLife Benchmark Results\n";
    std::cout << "=======================\n";
    std::cout << "Architecture: " << VLifeBenchmark::getArchitectureInfo() << "\n";

    if (quickMode) {
        std::cout << "Mode: Quick (reduced iterations)\n";
    } else {
        std::cout << "Mode: Full\n";
    }

    // Prime CPU to ensure consistent benchmark results
    if (!skipPrime) {
        std::cout << "Priming CPU (500ms)...\n";
        primeCPU(5000);
        std::cout << "CPU primed.\n";
    } else {
        std::cout << "CPU priming: skipped (--no-prime)\n";
    }

    // Benchmark 1: Glider Guns (sustained activity)
    {
        VLife board;
        int gunCount = quickMode ? 4 : 10;
        BenchmarkPatterns::setupGliderGuns(board, gunCount);

        int warmup = quickMode ? 50 : 100;
        int measured = quickMode ? 500 : 1000;

        auto result = VLifeBenchmark::runBenchmark(board, warmup, measured);
        printResult("Glider Gun (" + std::to_string(gunCount) + " guns)", result, warmup, measured);
    }

    // Benchmark 2: Random Soup (high activity)
    {
        VLife board;
        int size = quickMode ? 128 : 256;
        BenchmarkPatterns::setupRandomSoup(board, size, size, 0.3);

        int warmup = quickMode ? 25 : 50;
        int measured = quickMode ? 250 : 500;

        auto result = VLifeBenchmark::runBenchmark(board, warmup, measured);
        printResult("Random Soup (" + std::to_string(size) + "x" + std::to_string(size) + ", 30% density)",
                    result, warmup, measured);
    }

    // Benchmark 3: Spaceship Fleet (moving patterns)
    {
        VLife board;
        int count = quickMode ? 25 : 50;
        BenchmarkPatterns::setupSpaceships(board, count);

        int warmup = quickMode ? 50 : 100;
        int measured = quickMode ? 500 : 1000;

        auto result = VLifeBenchmark::runBenchmark(board, warmup, measured);
        printResult("Spaceship Fleet (" + std::to_string(count) + " ships)", result, warmup, measured);
    }

    // Benchmark 4: Block Grid (minimal changes, pure overhead test)
    {
        VLife board;
        int gridSize = quickMode ? 32 : 64;
        BenchmarkPatterns::setupBlockGrid(board, gridSize);

        int warmup = quickMode ? 10 : 20;
        int measured = quickMode ? 100 : 200;

        auto result = VLifeBenchmark::runBenchmark(board, warmup, measured);
        printResult("Block Grid (" + std::to_string(gridSize) + "x" + std::to_string(gridSize) + " blocks)",
                    result, warmup, measured);
    }

    // Benchmark 5: Gliders (tests tile boundary crossing)
    {
        VLife board;
        int count = quickMode ? 50 : 100;
        BenchmarkPatterns::setupGliders(board, count);

        int warmup = quickMode ? 100 : 200;
        int measured = quickMode ? 500 : 1000;

        auto result = VLifeBenchmark::runBenchmark(board, warmup, measured);
        printResult("Gliders (" + std::to_string(count) + " gliders)", result, warmup, measured);
    }

    // Benchmark 6: Single Glider (50k generations - long-running minimal pattern)
    {
        VLife board;
        BenchmarkPatterns::setupGlider(board, 0, 0);

        int warmup = quickMode ? 500 : 1000;
        int measured = quickMode ? 10000 : 50000;

        auto result = VLifeBenchmark::runBenchmark(board, warmup, measured);
        printResult("Single Glider (50k gen)", result, warmup, measured);
    }

    // Benchmark 7: Acorn (methuselah - grows then stabilizes)
    {
        VLife board;
        BenchmarkPatterns::setupAcorn(board);

        int warmup = quickMode ? 1000 : 2000;
        int measured = quickMode ? 2000 : 5000;

        auto result = VLifeBenchmark::runBenchmark(board, warmup, measured);
        printResult("Acorn (methuselah)", result, warmup, measured);
    }

    // Benchmark 8: Parallel vs Sequential Comparison (Large Random Soup)
    // Use larger pattern to exceed the 64 tile threshold for parallel processing
    {
        std::cout << "\n--- Parallel vs Sequential Comparison ---\n";

        // 512x512 = 16x16 tiles = 256 tiles, well above the 64 tile threshold
        int size = quickMode ? 512 : 1024;
        int warmup = quickMode ? 10 : 25;
        int measured = quickMode ? 50 : 100;

        std::cout << "Pattern size: " << size << "x" << size << " (expected ~"
                  << (size/32)*(size/32) << " tiles)\n";

        // Sequential benchmark
        VLife seqBoard;
        BenchmarkPatterns::setupRandomSoup(seqBoard, size, size, 0.3);
        seqBoard.setParallelEnabled(false);

        auto seqResult = VLifeBenchmark::runBenchmark(seqBoard, warmup, measured);
        std::cout << "\nSequential:\n";
        std::cout << std::fixed << std::setprecision(1);
        std::cout << "  Result: " << seqResult.generationsPerSecond << " gen/sec ("
                  << seqResult.avgMicrosPerGeneration << " us/gen)\n";
        std::cout << "  Tiles: peak=" << seqResult.peakTileCount << "\n";

        // Parallel benchmark
        VLife parBoard;
        BenchmarkPatterns::setupRandomSoup(parBoard, size, size, 0.3);
        parBoard.setParallelEnabled(true);

        auto parResult = VLifeBenchmark::runBenchmark(parBoard, warmup, measured);
        std::cout << "\nParallel:\n";
        std::cout << "  Result: " << parResult.generationsPerSecond << " gen/sec ("
                  << parResult.avgMicrosPerGeneration << " us/gen)\n";
        std::cout << "  Tiles: peak=" << parResult.peakTileCount << "\n";

        // Calculate speedup
        double speedup = parResult.generationsPerSecond / seqResult.generationsPerSecond;
        std::cout << "\nSpeedup: " << std::setprecision(2) << speedup << "x\n";

        // Hardware thread count
        unsigned int hwThreads = std::thread::hardware_concurrency();
        std::cout << "Hardware threads: " << hwThreads << "\n";
        std::cout << "Parallel efficiency: " << std::setprecision(1)
                  << (speedup / hwThreads * 100) << "%\n";
    }

    std::cout << "\n=======================\n";
    std::cout << "Benchmark complete.\n";

    return 0;
}

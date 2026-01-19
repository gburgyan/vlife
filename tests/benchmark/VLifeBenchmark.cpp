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

#include "../../src/vlife/VLife.h"
#include "BenchmarkPatterns.h"

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
        // Count tiles by checking if any tiles exist
        // We use a workaround since tiles is private
        size_t count = 0;
        // Scan a reasonable area to estimate tile count
        // This is an approximation for benchmark purposes
        for (int ty = -10; ty <= 50; ty++) {
            for (int tx = -10; tx <= 50; tx++) {
                if (board.getTileIfExists(tx, ty) != nullptr) {
                    count++;
                }
            }
        }
        return count;
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

int main(int argc, char* argv[]) {
    std::cout << "VLife Benchmark Results\n";
    std::cout << "=======================\n";
    std::cout << "Architecture: " << VLifeBenchmark::getArchitectureInfo() << "\n";

    // Determine if running quick or full benchmark
    bool quickMode = false;
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--quick" || std::string(argv[i]) == "-q") {
            quickMode = true;
        }
    }

    if (quickMode) {
        std::cout << "Mode: Quick (reduced iterations)\n";
    } else {
        std::cout << "Mode: Full\n";
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

    // Benchmark 6: Acorn (methuselah - grows then stabilizes)
    {
        VLife board;
        BenchmarkPatterns::setupAcorn(board);

        int warmup = quickMode ? 1000 : 2000;
        int measured = quickMode ? 2000 : 5000;

        auto result = VLifeBenchmark::runBenchmark(board, warmup, measured);
        printResult("Acorn (methuselah)", result, warmup, measured);
    }

    // Benchmark 7: Parallel vs Sequential Comparison (Large Random Soup)
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

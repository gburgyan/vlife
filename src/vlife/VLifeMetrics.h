//
// VLifeMetrics.h - Optional metrics collection for VLife
//
// When VLIFE_METRICS_ENABLED is defined, this provides per-generation metrics
// to support research paper claims about activity-proportional complexity.
//
// When not defined, all macros expand to no-ops for zero performance impact.
//

#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <chrono>
#include <atomic>

#ifdef VLIFE_METRICS_ENABLED

// Forward declaration
class VLife;

// Thread-local counters to avoid atomic contention during parallel processing
// Merged at phase boundaries
struct ThreadLocalCounters {
    uint64_t cellsBorn = 0;
    uint64_t cellsDied = 0;
    uint64_t boundaryCrossings = 0;
    uint64_t atomicOperations = 0;

    void reset() {
        cellsBorn = 0;
        cellsDied = 0;
        boundaryCrossings = 0;
        atomicOperations = 0;
    }

    void merge(const ThreadLocalCounters& other) {
        cellsBorn += other.cellsBorn;
        cellsDied += other.cellsDied;
        boundaryCrossings += other.boundaryCrossings;
        atomicOperations += other.atomicOperations;
    }
};

// Per-generation metrics snapshot
struct GenerationMetrics {
    // Generation number
    uint64_t generation = 0;

    // Primary metrics (k and N)
    uint64_t cellsBorn = 0;          // Cells that became alive
    uint64_t cellsDied = 0;          // Cells that died
    uint64_t totalLiveCells = 0;     // N - population at end of generation

    // Derived: k = cellsBorn + cellsDied (state changes)
    uint64_t getK() const { return cellsBorn + cellsDied; }

    // Activity ratio: k/N (core theorem validation)
    double getActivityRatio() const {
        return totalLiveCells > 0 ? static_cast<double>(getK()) / totalLiveCells : 0.0;
    }

    // Tile metrics
    uint64_t phase1ActiveTiles = 0;  // Tiles processed in Phase 1 (had activity)
    uint64_t phase2AllTiles = 0;     // Total tiles processed in Phase 2
    uint64_t totalTiles = 0;         // Total tiles at generation end
    uint64_t tilesCreated = 0;       // New tiles created this generation
    uint64_t tilesEvicted = 0;       // Dead tiles evicted this generation

    // Board extent (sparse vs dense comparison)
    int32_t minTileX = 0;
    int32_t maxTileX = 0;
    int32_t minTileY = 0;
    int32_t maxTileY = 0;

    // Extent area: (maxX - minX + 1) * (maxY - minY + 1) in tiles
    uint64_t getExtentArea() const {
        if (totalTiles == 0) return 0;
        return static_cast<uint64_t>(maxTileX - minTileX + 1) *
               static_cast<uint64_t>(maxTileY - minTileY + 1);
    }

    // Spatial density: N / (extent tiles * 1024)
    double getSpatialDensity() const {
        uint64_t extentCells = getExtentArea() * 1024;
        return extentCells > 0 ? static_cast<double>(totalLiveCells) / extentCells : 0.0;
    }

    // Timing breakdown (microseconds)
    uint64_t phase1TimeMicros = 0;   // Rule evaluation phase
    uint64_t phase2TimeMicros = 0;   // Neighbor update phase
    uint64_t totalTimeMicros = 0;    // Full generation time

    // Skip/Boundary metrics (proving optimizations work)
    uint64_t boundaryCrossings = 0;  // Cross-tile neighbor updates
    uint64_t atomicOperations = 0;   // CAS operations performed

    // Growth/Stability metrics (for methuselahs)
    int64_t populationDelta = 0;     // N_g - N_{g-1}
    uint64_t peakPopulation = 0;     // Maximum population seen so far
    uint64_t generationsSinceChange = 0; // Gens since last state change
    bool isStable = false;           // True when population stops changing
};

// MetricsCollector - manages generation metrics collection
class MetricsCollector {
public:
    MetricsCollector();
    ~MetricsCollector();

    // Reset all metrics (call before starting a new run)
    void reset();

    // Begin a new generation (called at start of runGeneration)
    void beginGeneration(uint64_t generationNumber, size_t tileCount);

    // End Phase 1 (rule evaluation)
    void endPhase1(size_t activeTiles, uint64_t timeMicros);

    // End Phase 2 (neighbor updates)
    void endPhase2(size_t allTiles, uint64_t timeMicros);

    // End generation (finalize and store metrics)
    void endGeneration(uint64_t totalLiveCells, size_t finalTileCount,
                       int32_t minTileX, int32_t maxTileX,
                       int32_t minTileY, int32_t maxTileY,
                       uint64_t totalTimeMicros,
                       size_t tilesCreated, size_t tilesEvicted);

    // Thread-local counter access (for parallel accumulation)
    static thread_local ThreadLocalCounters tlCounters;

    // Merge thread-local counters into current generation
    void mergeThreadLocalCounters(const ThreadLocalCounters& counters);

    // Get all collected metrics
    const std::vector<GenerationMetrics>& getMetrics() const { return metrics; }

    // Get the most recent generation's metrics
    const GenerationMetrics& getCurrentMetrics() const { return currentMetrics; }

    // Export functions
    void exportCSV(const std::string& filename) const;
    void exportJSON(const std::string& filename, const std::string& patternName = "",
                    const std::string& notes = "") const;

    // Summary statistics
    struct Summary {
        uint64_t totalGenerations = 0;
        uint64_t totalK = 0;           // Sum of all state changes
        double avgActivityRatio = 0.0;
        double maxActivityRatio = 0.0;
        uint64_t peakPopulation = 0;
        uint64_t peakTiles = 0;
        double avgBoundaryCrossingsPerGen = 0.0;
        double avgAtomicOpsPerGen = 0.0;
    };

    Summary getSummary() const;

private:
    std::vector<GenerationMetrics> metrics;
    GenerationMetrics currentMetrics;
    uint64_t previousPopulation = 0;
    uint64_t peakPopulationEver = 0;
    uint64_t stableGenerationCount = 0;

    static constexpr uint64_t STABILITY_THRESHOLD = 10; // Generations with no change
};

// ============================================================================
// Zero-overhead macros (expand to actual code only when metrics enabled)
// ============================================================================

// Increment counters (thread-local for parallel safety)
#define VLIFE_METRICS_INC_BORN(n) \
    MetricsCollector::tlCounters.cellsBorn += (n)

#define VLIFE_METRICS_INC_DIED(n) \
    MetricsCollector::tlCounters.cellsDied += (n)

#define VLIFE_METRICS_INC_BOUNDARY() \
    MetricsCollector::tlCounters.boundaryCrossings++

#define VLIFE_METRICS_INC_ATOMIC() \
    MetricsCollector::tlCounters.atomicOperations++

// Generation lifecycle (used by VLife.cpp)
#define VLIFE_METRICS_BEGIN_GEN(collector, gen, tileCount) \
    (collector).beginGeneration(gen, tileCount)

#define VLIFE_METRICS_END_PHASE1(collector, activeTiles, timeMicros) \
    (collector).endPhase1(activeTiles, timeMicros)

#define VLIFE_METRICS_END_PHASE2(collector, allTiles, timeMicros) \
    (collector).endPhase2(allTiles, timeMicros)

#define VLIFE_METRICS_MERGE_TL(collector) \
    (collector).mergeThreadLocalCounters(MetricsCollector::tlCounters); \
    MetricsCollector::tlCounters.reset()

#define VLIFE_METRICS_END_GEN(collector, liveCells, tileCount, minX, maxX, minY, maxY, totalTime, created, evicted) \
    (collector).endGeneration(liveCells, tileCount, minX, maxX, minY, maxY, totalTime, created, evicted)

// Timer helper
#define VLIFE_METRICS_TIMER_START() \
    auto _metrics_timer_start = std::chrono::high_resolution_clock::now()

#define VLIFE_METRICS_TIMER_MICROS() \
    std::chrono::duration_cast<std::chrono::microseconds>( \
        std::chrono::high_resolution_clock::now() - _metrics_timer_start).count()

#else // !VLIFE_METRICS_ENABLED

// ============================================================================
// No-op macros when metrics are disabled
// ============================================================================

#define VLIFE_METRICS_INC_BORN(n) ((void)0)
#define VLIFE_METRICS_INC_DIED(n) ((void)0)
#define VLIFE_METRICS_INC_BOUNDARY() ((void)0)
#define VLIFE_METRICS_INC_ATOMIC() ((void)0)

#define VLIFE_METRICS_BEGIN_GEN(collector, gen, tileCount) ((void)0)
#define VLIFE_METRICS_END_PHASE1(collector, activeTiles, timeMicros) ((void)0)
#define VLIFE_METRICS_END_PHASE2(collector, allTiles, timeMicros) ((void)0)
#define VLIFE_METRICS_MERGE_TL(collector) ((void)0)
#define VLIFE_METRICS_END_GEN(collector, liveCells, tileCount, minX, maxX, minY, maxY, totalTime, created, evicted) ((void)0)

#define VLIFE_METRICS_TIMER_START() ((void)0)
#define VLIFE_METRICS_TIMER_MICROS() 0

#endif // VLIFE_METRICS_ENABLED

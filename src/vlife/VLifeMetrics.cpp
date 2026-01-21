//
// VLifeMetrics.cpp - Implementation of metrics collection
//

#include "VLifeMetrics.h"

#ifdef VLIFE_METRICS_ENABLED

#include <fstream>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <algorithm>

// Thread-local counters definition
thread_local ThreadLocalCounters MetricsCollector::tlCounters;

MetricsCollector::MetricsCollector() {
    reset();
}

MetricsCollector::~MetricsCollector() = default;

void MetricsCollector::reset() {
    metrics.clear();
    currentMetrics = GenerationMetrics{};
    previousPopulation = 0;
    peakPopulationEver = 0;
    stableGenerationCount = 0;
    tlCounters.reset();
}

void MetricsCollector::beginGeneration(uint64_t generationNumber, size_t tileCount) {
    currentMetrics = GenerationMetrics{};
    currentMetrics.generation = generationNumber;
    currentMetrics.totalTiles = tileCount;
    tlCounters.reset();
}

void MetricsCollector::endPhase1(size_t activeTiles, uint64_t timeMicros) {
    currentMetrics.phase1ActiveTiles = activeTiles;
    currentMetrics.phase1TimeMicros = timeMicros;
}

void MetricsCollector::endPhase2(size_t allTiles, uint64_t timeMicros) {
    currentMetrics.phase2AllTiles = allTiles;
    currentMetrics.phase2TimeMicros = timeMicros;
}

void MetricsCollector::mergeThreadLocalCounters(const ThreadLocalCounters& counters) {
    currentMetrics.cellsBorn += counters.cellsBorn;
    currentMetrics.cellsDied += counters.cellsDied;
    currentMetrics.activeWords += counters.activeWords;
    currentMetrics.boundaryCrossings += counters.boundaryCrossings;
    currentMetrics.atomicOperations += counters.atomicOperations;
}

void MetricsCollector::endGeneration(uint64_t totalLiveCells, size_t finalTileCount,
                                      int32_t minTileX, int32_t maxTileX,
                                      int32_t minTileY, int32_t maxTileY,
                                      uint64_t totalTimeMicros,
                                      size_t tilesCreated, size_t tilesEvicted) {
    currentMetrics.totalLiveCells = totalLiveCells;
    currentMetrics.totalTiles = finalTileCount;
    currentMetrics.minTileX = minTileX;
    currentMetrics.maxTileX = maxTileX;
    currentMetrics.minTileY = minTileY;
    currentMetrics.maxTileY = maxTileY;
    currentMetrics.totalTimeMicros = totalTimeMicros;
    currentMetrics.tilesCreated = tilesCreated;
    currentMetrics.tilesEvicted = tilesEvicted;

    // Population delta
    currentMetrics.populationDelta = static_cast<int64_t>(totalLiveCells) -
                                      static_cast<int64_t>(previousPopulation);
    previousPopulation = totalLiveCells;

    // Peak population tracking
    if (totalLiveCells > peakPopulationEver) {
        peakPopulationEver = totalLiveCells;
    }
    currentMetrics.peakPopulation = peakPopulationEver;

    // Stability detection
    if (currentMetrics.getK() == 0) {
        stableGenerationCount++;
    } else {
        stableGenerationCount = 0;
    }
    currentMetrics.generationsSinceChange = stableGenerationCount;
    currentMetrics.isStable = (stableGenerationCount >= STABILITY_THRESHOLD);

    // Store the completed metrics
    metrics.push_back(currentMetrics);
}

void MetricsCollector::exportCSV(const std::string& filename) const {
    std::ofstream out(filename);
    if (!out.is_open()) {
        return;
    }

    // Header
    out << "generation,cells_born,cells_died,k,total_live_cells,activity_ratio,"
        << "population_delta,peak_population,gens_since_change,is_stable,"
        << "phase1_active_tiles,phase2_all_tiles,total_tiles,tiles_created,tiles_evicted,"
        << "min_tile_x,max_tile_x,min_tile_y,max_tile_y,extent_area,spatial_density,"
        << "phase1_time_us,phase2_time_us,total_time_us,"
        << "active_words,boundary_crossings,atomic_operations\n";

    // Data rows
    out << std::fixed << std::setprecision(6);
    for (const auto& m : metrics) {
        out << m.generation << ","
            << m.cellsBorn << ","
            << m.cellsDied << ","
            << m.getK() << ","
            << m.totalLiveCells << ","
            << m.getActivityRatio() << ","
            << m.populationDelta << ","
            << m.peakPopulation << ","
            << m.generationsSinceChange << ","
            << (m.isStable ? 1 : 0) << ","
            << m.phase1ActiveTiles << ","
            << m.phase2AllTiles << ","
            << m.totalTiles << ","
            << m.tilesCreated << ","
            << m.tilesEvicted << ","
            << m.minTileX << ","
            << m.maxTileX << ","
            << m.minTileY << ","
            << m.maxTileY << ","
            << m.getExtentArea() << ","
            << m.getSpatialDensity() << ","
            << m.phase1TimeMicros << ","
            << m.phase2TimeMicros << ","
            << m.totalTimeMicros << ","
            << m.activeWords << ","
            << m.boundaryCrossings << ","
            << m.atomicOperations << "\n";
    }

    out.close();
}

void MetricsCollector::exportJSON(const std::string& filename,
                                   const std::string& patternName,
                                   const std::string& notes) const {
    std::ofstream out(filename);
    if (!out.is_open()) {
        return;
    }

    out << std::fixed << std::setprecision(6);
    out << "{\n";

    // Metadata
    out << "  \"metadata\": {\n";
    out << "    \"pattern\": \"" << patternName << "\",\n";
    out << "    \"total_generations\": " << metrics.size() << ",\n";
    out << "    \"notes\": \"" << notes << "\"\n";
    out << "  },\n";

    // Summary
    Summary summary = getSummary();
    out << "  \"summary\": {\n";
    out << "    \"total_generations\": " << summary.totalGenerations << ",\n";
    out << "    \"total_k\": " << summary.totalK << ",\n";
    out << "    \"avg_activity_ratio\": " << summary.avgActivityRatio << ",\n";
    out << "    \"max_activity_ratio\": " << summary.maxActivityRatio << ",\n";
    out << "    \"peak_population\": " << summary.peakPopulation << ",\n";
    out << "    \"peak_tiles\": " << summary.peakTiles << ",\n";
    out << "    \"avg_boundary_crossings_per_gen\": " << summary.avgBoundaryCrossingsPerGen << ",\n";
    out << "    \"avg_atomic_ops_per_gen\": " << summary.avgAtomicOpsPerGen << "\n";
    out << "  },\n";

    // Generations array
    out << "  \"generations\": [\n";
    for (size_t i = 0; i < metrics.size(); i++) {
        const auto& m = metrics[i];
        out << "    {\n";
        out << "      \"generation\": " << m.generation << ",\n";
        out << "      \"cells_born\": " << m.cellsBorn << ",\n";
        out << "      \"cells_died\": " << m.cellsDied << ",\n";
        out << "      \"k\": " << m.getK() << ",\n";
        out << "      \"total_live_cells\": " << m.totalLiveCells << ",\n";
        out << "      \"activity_ratio\": " << m.getActivityRatio() << ",\n";
        out << "      \"population_delta\": " << m.populationDelta << ",\n";
        out << "      \"peak_population\": " << m.peakPopulation << ",\n";
        out << "      \"is_stable\": " << (m.isStable ? "true" : "false") << ",\n";
        out << "      \"tiles\": {\n";
        out << "        \"phase1_active\": " << m.phase1ActiveTiles << ",\n";
        out << "        \"phase2_all\": " << m.phase2AllTiles << ",\n";
        out << "        \"total\": " << m.totalTiles << ",\n";
        out << "        \"created\": " << m.tilesCreated << ",\n";
        out << "        \"evicted\": " << m.tilesEvicted << "\n";
        out << "      },\n";
        out << "      \"extent\": {\n";
        out << "        \"min_tile_x\": " << m.minTileX << ",\n";
        out << "        \"max_tile_x\": " << m.maxTileX << ",\n";
        out << "        \"min_tile_y\": " << m.minTileY << ",\n";
        out << "        \"max_tile_y\": " << m.maxTileY << ",\n";
        out << "        \"area\": " << m.getExtentArea() << ",\n";
        out << "        \"spatial_density\": " << m.getSpatialDensity() << "\n";
        out << "      },\n";
        out << "      \"timing_us\": {\n";
        out << "        \"phase1\": " << m.phase1TimeMicros << ",\n";
        out << "        \"phase2\": " << m.phase2TimeMicros << ",\n";
        out << "        \"total\": " << m.totalTimeMicros << "\n";
        out << "      },\n";
        out << "      \"optimization_metrics\": {\n";
        out << "        \"active_words\": " << m.activeWords << ",\n";
        out << "        \"boundary_crossings\": " << m.boundaryCrossings << ",\n";
        out << "        \"atomic_operations\": " << m.atomicOperations << "\n";
        out << "      }\n";
        out << "    }";
        if (i < metrics.size() - 1) {
            out << ",";
        }
        out << "\n";
    }
    out << "  ]\n";
    out << "}\n";

    out.close();
}

MetricsCollector::Summary MetricsCollector::getSummary() const {
    Summary summary;
    if (metrics.empty()) {
        return summary;
    }

    summary.totalGenerations = metrics.size();

    double activitySum = 0.0;
    uint64_t totalBoundary = 0;
    uint64_t totalAtomic = 0;

    for (const auto& m : metrics) {
        summary.totalK += m.getK();

        double ratio = m.getActivityRatio();
        activitySum += ratio;
        if (ratio > summary.maxActivityRatio) {
            summary.maxActivityRatio = ratio;
        }

        if (m.peakPopulation > summary.peakPopulation) {
            summary.peakPopulation = m.peakPopulation;
        }

        if (m.totalTiles > summary.peakTiles) {
            summary.peakTiles = m.totalTiles;
        }

        totalBoundary += m.boundaryCrossings;
        totalAtomic += m.atomicOperations;
    }

    summary.avgActivityRatio = activitySum / metrics.size();
    summary.avgBoundaryCrossingsPerGen = static_cast<double>(totalBoundary) / metrics.size();
    summary.avgAtomicOpsPerGen = static_cast<double>(totalAtomic) / metrics.size();

    return summary;
}

#endif // VLIFE_METRICS_ENABLED

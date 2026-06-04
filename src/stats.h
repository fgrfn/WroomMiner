// ============================================================
//  stats.h - Hashrate tracking and mining statistics
// ============================================================
#pragma once

#include <Arduino.h>
#include <atomic>

namespace WroomMiner {

// Thread-safe mining statistics. Written by the mining task
// (Core 1) and read by the API server (Core 0).
struct MiningStats {
    // --- Live counters (atomic for cross-core access) ---
    std::atomic<uint64_t> hashCount{0};        // hashes since last reset
    std::atomic<uint32_t> sharesAccepted{0};
    std::atomic<uint32_t> sharesRejected{0};
    std::atomic<uint32_t> blocksFound{0};

    // --- Best difficulty ---
    std::atomic<double>   bestDifficulty{0.0};
    std::atomic<double>   sessionBestDiff{0.0};

    // --- Timestamps ---
    uint32_t startupMillis = 0;
    uint32_t lastShareMillis = 0;

    // --- Current hashrate (1s / 1min average) ---
    std::atomic<float>    hashrate1s{0.0f};
    std::atomic<float>    hashrate1m{0.0f};
};

class Stats {
public:
    // Singleton access
    static MiningStats& get();

    // Called by the mining task when N hashes have been computed.
    static void recordHashes(uint32_t count);

    // Called by the Stratum client.
    static void recordShareAccepted(double difficulty);
    static void recordShareRejected();
    static void recordBlockFound();

    // Called once per second by the stats task to compute the
    // 1s / 1min hashrate average.
    static void tick();

    // Persists best diff and block counter to NVS.
    static void persist();

    // Restores persistent values at boot.
    static void restore();

    // Uptime in seconds since boot.
    static uint32_t uptimeSeconds();
};

} // namespace WroomMiner

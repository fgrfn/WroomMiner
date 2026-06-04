// ============================================================
//  stats.cpp - Hashrate tracking implementation
// ============================================================
#include "stats.h"
#include <Preferences.h>

namespace WroomMiner {

static MiningStats g_stats;
static const char* NVS_NAMESPACE = "wmstats";

// Ring buffer for the 1-minute hashrate average (60x 1s samples)
static float    g_hashrateRing[60] = {0};
static uint8_t  g_hashrateRingIdx  = 0;
static uint64_t g_lastHashCount    = 0;
static uint32_t g_lastTickMs       = 0;

MiningStats& Stats::get() {
    return g_stats;
}

void Stats::recordHashes(uint32_t count) {
    g_stats.hashCount.fetch_add(count, std::memory_order_relaxed);
}

void Stats::recordShareAccepted(double difficulty) {
    g_stats.sharesAccepted.fetch_add(1, std::memory_order_relaxed);
    g_stats.lastShareMillis = millis();

    // Update session best
    double curSessionBest = g_stats.sessionBestDiff.load();
    while (difficulty > curSessionBest &&
           !g_stats.sessionBestDiff.compare_exchange_weak(curSessionBest, difficulty)) {
        // retry
    }

    // Update all-time best
    double curBest = g_stats.bestDifficulty.load();
    while (difficulty > curBest &&
           !g_stats.bestDifficulty.compare_exchange_weak(curBest, difficulty)) {
        // retry
    }
}

void Stats::recordShareRejected() {
    g_stats.sharesRejected.fetch_add(1, std::memory_order_relaxed);
}

void Stats::recordBlockFound() {
    g_stats.blocksFound.fetch_add(1, std::memory_order_relaxed);
}

void Stats::tick() {
    uint32_t now = millis();
    if (g_lastTickMs == 0) {
        g_lastTickMs = now;
        g_lastHashCount = g_stats.hashCount.load();
        return;
    }

    uint32_t elapsedMs = now - g_lastTickMs;
    if (elapsedMs < 100) return; // at least 100ms

    uint64_t curHashes = g_stats.hashCount.load();
    uint64_t delta = curHashes - g_lastHashCount;

    // Hashes per second (in H/s)
    float hps = (delta * 1000.0f) / elapsedMs;
    g_stats.hashrate1s.store(hps);

    // 1-minute average over the ring buffer
    g_hashrateRing[g_hashrateRingIdx] = hps;
    g_hashrateRingIdx = (g_hashrateRingIdx + 1) % 60;

    float sum = 0;
    for (int i = 0; i < 60; ++i) sum += g_hashrateRing[i];
    g_stats.hashrate1m.store(sum / 60.0f);

    g_lastTickMs = now;
    g_lastHashCount = curHashes;
}

void Stats::persist() {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) return;
    prefs.putDouble("bestdiff", g_stats.bestDifficulty.load());
    prefs.putUInt  ("blocks",   g_stats.blocksFound.load());
    prefs.end();
}

void Stats::restore() {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, true)) return;
    g_stats.bestDifficulty.store(prefs.getDouble("bestdiff", 0.0));
    g_stats.blocksFound.store(prefs.getUInt("blocks", 0));
    g_stats.startupMillis = millis();
    prefs.end();
}

uint32_t Stats::uptimeSeconds() {
    return (millis() - g_stats.startupMillis) / 1000;
}

} // namespace WroomMiner

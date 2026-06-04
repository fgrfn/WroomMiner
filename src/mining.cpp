// ============================================================
//  mining.cpp - Mining task implementation
//
//  Phase 1: naive loop without midstate optimization.
//  Expected hashrate: 80-120 KH/s per core (~200 KH/s total).
//
//  Phase 3 will:
//    - add the midstate trick    (-> ~3x speedup)
//    - use hardware SHA256        (-> ~2x speedup)
//    - utilize both cores         (-> ~2x speedup)
//  Target: ~800 KH/s
// ============================================================
#include "mining.h"
#include "sha256d.h"
#include "stats.h"
#include <esp_log.h>
#include <cstring>

namespace WroomMiner {

MiningEngine::MiningEngine() {}

void MiningEngine::start(StratumClient* stratum) {
    _stratum = stratum;
    _running = true;

    // Pin the mining task to Core 1 (APP_CPU).
    // Core 0 (PRO_CPU) stays free for WiFi, HTTP, Stratum.
    xTaskCreatePinnedToCore(
        taskTrampoline,
        "mining",
        MINING_TASK_STACK,
        this,
        MINING_TASK_PRIORITY,
        &_taskHandle,
        1 // Core 1
    );
}

void MiningEngine::stop() {
    _running = false;
    if (_taskHandle) {
        // The task terminates itself via _running = false
        _taskHandle = nullptr;
    }
}

void MiningEngine::onNewJob(const StratumJob& job) {
    _activeJob = job;
    _jobEpoch++;  // Signal to the task that the old work is stale
    log_i("Mining: new job %s, epoch %u", job.jobId.c_str(), _jobEpoch);
}

void MiningEngine::taskTrampoline(void* arg) {
    static_cast<MiningEngine*>(arg)->taskLoop();
    vTaskDelete(nullptr);
}

void MiningEngine::buildHeader(const StratumJob& job,
                               uint32_t extranonce2,
                               uint8_t* header80) {
    // === Bitcoin block header layout (80 bytes, little-endian) ===
    //   bytes  0- 3: version           (4 B)
    //   bytes  4-35: prevBlockHash      (32 B)
    //   bytes 36-67: merkleRoot         (32 B)
    //   bytes 68-71: nTime              (4 B)
    //   bytes 72-75: nBits              (4 B)
    //   bytes 76-79: nonce              (4 B, set by the mining loop)

    // Version (little-endian)
    header80[0] = (job.version >> 0)  & 0xFF;
    header80[1] = (job.version >> 8)  & 0xFF;
    header80[2] = (job.version >> 16) & 0xFF;
    header80[3] = (job.version >> 24) & 0xFF;

    // prevHash (Stratum provides big-endian, header needs little-endian)
    // Simplified in Phase 1: byte-reverse the whole 32-byte field
    for (int i = 0; i < 32; ++i) {
        header80[4 + i] = job.prevHash[31 - i];
    }

    // TODO Phase 1: compute the Merkle root.
    //   1. coinbase = coinbase1 + extranonce1 + extranonce2 + coinbase2
    //   2. merkleRoot = sha256d(coinbase)
    //   3. for each branch in job.merkleBranches:
    //        merkleRoot = sha256d(merkleRoot + branch)
    //   4. write merkleRoot into header80[36..67]
    // Current placeholder (shares will be REJECTED until implemented):
    memset(header80 + 36, 0, 32);

    // nTime (little-endian)
    header80[68] = (job.nTime >> 0)  & 0xFF;
    header80[69] = (job.nTime >> 8)  & 0xFF;
    header80[70] = (job.nTime >> 16) & 0xFF;
    header80[71] = (job.nTime >> 24) & 0xFF;

    // nBits (little-endian)
    header80[72] = (job.nBits >> 0)  & 0xFF;
    header80[73] = (job.nBits >> 8)  & 0xFF;
    header80[74] = (job.nBits >> 16) & 0xFF;
    header80[75] = (job.nBits >> 24) & 0xFF;

    // Nonce is set inside the loop (bytes 76-79)
    header80[76] = 0;
    header80[77] = 0;
    header80[78] = 0;
    header80[79] = 0;
}

void MiningEngine::taskLoop() {
    log_i("Mining task started on core %d", xPortGetCoreID());

    uint8_t  header[80];
    uint8_t  hash[32];
    uint32_t localEpoch = 0;
    uint32_t extranonce2 = 0;
    uint32_t nonce = 0;
    uint32_t hashesSinceReport = 0;

    while (_running) {
        // Wait for the first job
        if (!_activeJob.valid) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // New job? Rebuild the header.
        if (localEpoch != _jobEpoch) {
            localEpoch = _jobEpoch;
            extranonce2 = 0;
            nonce = 0;
            buildHeader(_activeJob, extranonce2, header);
            log_d("Mining: rebuilt header for epoch %u", localEpoch);
        }

        // --- Hash a batch of nonces ---
        // We do 1024 hashes per iteration and then check whether a new
        // job has arrived - this keeps latency low.
        constexpr uint32_t BATCH = 1024;

        for (uint32_t i = 0; i < BATCH; ++i) {
            // Write the nonce into the header (little-endian)
            header[76] = (nonce >> 0)  & 0xFF;
            header[77] = (nonce >> 8)  & 0xFF;
            header[78] = (nonce >> 16) & 0xFF;
            header[79] = (nonce >> 24) & 0xFF;

            // Compute SHA256d
            sha256d(header, 80, hash);

            // Target check: simple variant that checks whether the
            // most significant 4 bytes (big-endian!) are small enough.
            // Bitcoin convention: the hash is little-endian, so we read
            // the last 4 bytes as a big 32-bit word.
            uint32_t hashHi = (uint32_t(hash[31]) << 24) |
                              (uint32_t(hash[30]) << 16) |
                              (uint32_t(hash[29]) << 8)  |
                              (uint32_t(hash[28]) << 0);

            // Phase 1: simple threshold match for diff=1 shares
            // (equivalent to hash[28..31] == 0x00000000ffffffff)
            if (hashHi == 0) {
                // Plausible share - submit to the pool
                char en2Hex[16];
                snprintf(en2Hex, sizeof(en2Hex), "%08x", extranonce2);

                bool sent = _stratum->submitShare(
                    _activeJob.jobId,
                    String(en2Hex),
                    _activeJob.nTime,
                    nonce
                );

                if (sent) {
                    log_i("Mining: SHARE submitted (nonce=%08x)", nonce);
                }
            }

            nonce++;
            hashesSinceReport++;

            // When the nonce range is exhausted, advance extranonce2
            if (nonce == 0) {
                extranonce2++;
                buildHeader(_activeJob, extranonce2, header);
            }
        }

        // Update stats
        Stats::recordHashes(BATCH);
        hashesSinceReport = 0;

        // Small yield so the watchdog doesn't bite
        // (1 tick at 1ms tick rate = 1ms pause per batch)
        if (uxTaskGetStackHighWaterMark(nullptr) < 1024) {
            log_w("Mining: low stack: %u bytes free",
                  uxTaskGetStackHighWaterMark(nullptr));
        }
        vTaskDelay(1);
    }

    log_i("Mining task exiting");
}

} // namespace WroomMiner

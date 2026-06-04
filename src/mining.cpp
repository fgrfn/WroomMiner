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
#include <cstdio>
#include <cstring>

namespace WroomMiner {

namespace {

int hexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool appendHexBytes(const String& hex, uint8_t* out, size_t outCapacity, size_t& outLen) {
    if ((hex.length() % 2) != 0) return false;

    size_t byteCount = hex.length() / 2;
    if (outLen + byteCount > outCapacity) return false;

    for (size_t i = 0; i < byteCount; ++i) {
        int hi = hexNibble(hex[i * 2]);
        int lo = hexNibble(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        out[outLen++] = uint8_t((hi << 4) | lo);
    }

    return true;
}

bool hexToBytes32(const String& hex, uint8_t* out) {
    if (hex.length() != 64) return false;

    size_t outLen = 0;
    return appendHexBytes(hex, out, 32, outLen) && outLen == 32;
}

void extranonce2ToHex(uint32_t extranonce2, uint8_t extranonce2Size, char* out, size_t outSize) {
    size_t hexLen = size_t(extranonce2Size) * 2;
    if (hexLen + 1 > outSize) {
        out[0] = '\0';
        return;
    }

    for (size_t i = 0; i < hexLen; ++i) {
        out[i] = '0';
    }
    out[hexLen] = '\0';

    char counterHex[9];
    snprintf(counterHex, sizeof(counterHex), "%08x", extranonce2);

    size_t copyLen = hexLen < 8 ? hexLen : 8;
    memcpy(out + hexLen - copyLen, counterHex + 8 - copyLen, copyLen);
}

void setDiff1Target(uint8_t* target) {
    memset(target, 0, 32);
    target[26] = 0xFF;
    target[27] = 0xFF;
}

bool multiplyTarget(uint8_t* target, uint32_t multiplier) {
    uint64_t carry = 0;
    for (size_t i = 0; i < 32; ++i) {
        uint64_t value = uint64_t(target[i]) * multiplier + carry;
        target[i] = uint8_t(value & 0xFF);
        carry = value >> 8;
    }
    return carry == 0;
}

void divideTarget(uint8_t* target, uint32_t divisor) {
    uint32_t remainder = 0;
    for (int i = 31; i >= 0; --i) {
        uint32_t value = (remainder << 8) | target[i];
        target[i] = uint8_t(value / divisor);
        remainder = value % divisor;
    }
}

void targetForDifficulty(double difficulty, uint8_t* target) {
    constexpr uint32_t DIFFICULTY_SCALE = 1000000;

    if (difficulty <= 0.0) {
        difficulty = 1.0;
    }

    double scaledDouble = difficulty * DIFFICULTY_SCALE;
    while (scaledDouble > 4000000000.0) {
        scaledDouble /= 10.0;
    }

    uint32_t scaledDifficulty = uint32_t(scaledDouble + 0.5);
    if (scaledDifficulty == 0) {
        scaledDifficulty = 1;
    }

    setDiff1Target(target);
    if (!multiplyTarget(target, DIFFICULTY_SCALE)) {
        memset(target, 0xFF, 32);
        return;
    }
    divideTarget(target, scaledDifficulty);
}

bool hashMeetsTarget(const uint8_t* hash, const uint8_t* target) {
    for (int i = 31; i >= 0; --i) {
        if (hash[i] < target[i]) return true;
        if (hash[i] > target[i]) return false;
    }
    return true;
}

} // namespace

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
    Serial.printf("[I/WM] Mining work queued : job=%s diff=%.6f branches=%u en2_size=%u\r\n",
                  job.jobId.c_str(),
                  job.difficulty,
                  job.merkleCount,
                  job.extranonce2Size);
    log_i("Mining: new job %s, epoch %u", job.jobId.c_str(), _jobEpoch);
}

void MiningEngine::onDifficulty(double difficulty) {
    if (!_activeJob.valid) {
        return;
    }

    _activeJob.difficulty = difficulty;
    _jobEpoch++;
    log_i("Mining: difficulty updated to %.6f, epoch %u", difficulty, _jobEpoch);
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

    // prevHash: Stratum sends each 4-byte word in big-endian; the block header
    // needs each word in little-endian (per-word byte swap, not a full 32-byte flip).
    for (int w = 0; w < 8; ++w)
        for (int b = 0; b < 4; ++b)
            header80[4 + w*4 + b] = job.prevHash[w*4 + (3 - b)];

    uint8_t coinbase[512];
    size_t coinbaseLen = 0;
    char extranonce2Hex[17];
    extranonce2ToHex(extranonce2, job.extranonce2Size, extranonce2Hex, sizeof(extranonce2Hex));

    bool merkleOk = extranonce2Hex[0] != '\0' &&
                    appendHexBytes(job.coinbase1, coinbase, sizeof(coinbase), coinbaseLen) &&
                    appendHexBytes(job.extranonce1, coinbase, sizeof(coinbase), coinbaseLen) &&
                    appendHexBytes(String(extranonce2Hex), coinbase, sizeof(coinbase), coinbaseLen) &&
                    appendHexBytes(job.coinbase2, coinbase, sizeof(coinbase), coinbaseLen);

    uint8_t merkleRoot[32];
    if (merkleOk) {
        sha256d(coinbase, coinbaseLen, merkleRoot);

        for (uint8_t branchIndex = 0; branchIndex < job.merkleCount; ++branchIndex) {
            uint8_t branch[32];
            uint8_t merkleInput[64];
            if (!hexToBytes32(job.merkleBranches[branchIndex], branch)) {
                merkleOk = false;
                break;
            }

            memcpy(merkleInput, merkleRoot, 32);
            memcpy(merkleInput + 32, branch, 32);
            sha256d(merkleInput, sizeof(merkleInput), merkleRoot);
        }
    }

    if (merkleOk) {
        for (int i = 0; i < 32; ++i) {
            header80[36 + i] = merkleRoot[31 - i];
        }
    } else {
        log_w("Mining: invalid Merkle data for job %s", job.jobId.c_str());
        memset(header80 + 36, 0, 32);
    }

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
    uint8_t  shareTarget[32];
    uint32_t localEpoch = 0;
    uint32_t extranonce2 = 0;
    uint32_t nonce = 0;
    uint32_t hashesSinceReport = 0;
    bool waitingForJobLogged = false;

    while (_running) {
        // Wait for the first job
        if (!_activeJob.valid) {
            if (!waitingForJobLogged) {
                Serial.println("[I/WM] Waiting for first mining job from pool");
                waitingForJobLogged = true;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // New job? Rebuild the header.
        if (localEpoch != _jobEpoch) {
            localEpoch = _jobEpoch;
            extranonce2 = 0;
            nonce = 0;
            buildHeader(_activeJob, extranonce2, header);
            targetForDifficulty(_activeJob.difficulty, shareTarget);
            waitingForJobLogged = false;
            Serial.printf("[I/WM] Mining work prepared : job=%s epoch=%lu diff=%.6f extranonce2=%08lx\r\n",
                          _activeJob.jobId.c_str(),
                          static_cast<unsigned long>(localEpoch),
                          _activeJob.difficulty,
                          static_cast<unsigned long>(extranonce2));
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

            // mbedTLS SHA256d output is big-endian (MSB at index 0).
            // hashMeetsTarget and the target use little-endian, so byte-reverse.
            for (int _i = 0, _j = 31; _i < _j; ++_i, --_j) {
                uint8_t _t = hash[_i]; hash[_i] = hash[_j]; hash[_j] = _t;
            }

            if (hashMeetsTarget(hash, shareTarget)) {
                // Plausible share - submit to the pool
                char en2Hex[17];
                extranonce2ToHex(extranonce2, _activeJob.extranonce2Size, en2Hex, sizeof(en2Hex));

                bool sent = _stratum->submitShare(
                    _activeJob.jobId,
                    String(en2Hex),
                    _activeJob.nTime,
                    nonce
                );

                if (sent) {
                    Serial.printf("[L/WM] Share candidate submitted : job=%s nonce=%08lx en2=%s pool_diff=%.6f\r\n",
                                  _activeJob.jobId.c_str(),
                                  static_cast<unsigned long>(nonce),
                                  en2Hex,
                                  _activeJob.difficulty);
                    log_i("Mining: SHARE submitted (nonce=%08x)", nonce);
                } else {
                    Serial.printf("[W/WM] Share candidate not submitted : job=%s nonce=%08lx\r\n",
                                  _activeJob.jobId.c_str(),
                                  static_cast<unsigned long>(nonce));
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

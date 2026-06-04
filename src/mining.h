// ============================================================
//  mining.h - Mining task (runs on Core 1)
//
//  Iterates over nonces, computes SHA256d, checks the target.
//  When a share is found, it is submitted to the pool via the
//  Stratum client.
// ============================================================
#pragma once

#include "stratum.h"

namespace WroomMiner {

class MiningEngine {
public:
    MiningEngine();

    // Starts the mining task pinned to Core 1.
    void start(StratumClient* stratum);

    void stop();

    // Called by the Stratum client when a new job is received.
    // Resets the current work.
    void onNewJob(const StratumJob& job);

    bool isRunning() const { return _running; }

private:
    static void taskTrampoline(void* arg);
    void taskLoop();

    // Builds the 80-byte block header from the Stratum job.
    // Writes 'extranonce2' (4-byte incrementing counter).
    void buildHeader(const StratumJob& job,
                     uint32_t extranonce2,
                     uint8_t* header80);

    StratumClient*       _stratum = nullptr;
    TaskHandle_t         _taskHandle = nullptr;
    volatile bool        _running = false;

    // Current job (copied from Stratum, lock-free via epoch counter)
    StratumJob           _activeJob;
    volatile uint32_t    _jobEpoch = 0; // incremented on every new job
};

} // namespace WroomMiner

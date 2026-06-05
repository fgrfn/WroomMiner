// ============================================================
//  stratum.h - Stratum v1 client
//
//  Implements the JSON-RPC protocol for Bitcoin mining pools:
//    - mining.subscribe
//    - mining.authorize
//    - mining.notify   (pool -> miner: new job)
//    - mining.submit   (miner -> pool: found share)
//    - mining.set_difficulty
// ============================================================
#pragma once

#include <Arduino.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include <functional>

namespace WroomMiner {

// A Stratum "job" received from the pool.
// The mining task builds the 80-byte block header from this.
struct StratumJob {
    String   jobId;
    uint8_t  prevHash[32];       // header byte order, as received from the pool
    String   coinbase1;          // hex
    String   coinbase2;          // hex
    String   merkleBranches[16]; // hex strings
    uint8_t  merkleCount = 0;
    uint32_t version;
    uint32_t nBits;              // current target
    uint32_t nTime;

    bool     cleanJobs = false;  // true => discard old jobs
    double   difficulty = 1.0;   // share difficulty from the pool

    // Set during subscribe:
    String   extranonce1;        // hex
    uint8_t  extranonce2Size = 4;

    bool     valid = false;
};

class StratumClient {
public:
    // Callbacks
    using JobCallback     = std::function<void(const StratumJob&)>;
    using SubmitCallback  = std::function<void(bool accepted, double diff)>;
    using DiffCallback    = std::function<void(double newDiff)>;

    StratumClient();
    ~StratumClient();

    // Set callback for new jobs.
    void onJob(JobCallback cb)        { _jobCb = cb; }
    void onSubmit(SubmitCallback cb)  { _submitCb = cb; }
    void onDifficulty(DiffCallback cb){ _diffCb = cb; }

    // Connect to the pool. Blocks until connected or timeout.
    bool connect(const String& host, uint16_t port,
                 const String& wallet, const String& worker,
                 const String& password,
                 double suggestedDifficulty = 0.0);

    void disconnect();

    bool isConnected() { return _client.connected(); }
    double currentDifficulty() const { return _currentDifficulty; }

    // Called by the mining task when a share is found.
    // extranonce2, nTime and nonce as hex strings.
    bool submitShare(const String& jobId,
                     const String& extranonce2,
                     uint32_t nTime,
                     uint32_t nonce);

    // Must be called regularly (e.g. every 50ms) to process
    // incoming messages.
    void loop();

    // Last received job (nullptr if none).
    const StratumJob* currentJob() const {
        return _currentJob.valid ? &_currentJob : nullptr;
    }

private:
    bool sendJson(const String& json);
    bool suggestDifficulty();
    void trackSubmitId(uint32_t id);
    bool consumeSubmitId(uint32_t id);
    void processLine(const String& line);
    void handleNotify(JsonArrayConst params);
    void handleSetDifficulty(JsonArrayConst params);
    void handleSubmitResponse(JsonVariantConst id, JsonVariantConst result, JsonVariantConst error);

    WiFiClient    _client;
    String        _rxBuffer;
    String        _authorizedWorker;
    String        _connectedHost;
    uint16_t      _connectedPort = 0;
    uint32_t      _msgId = 1;
    uint32_t      _subscribeId = 0;
    uint32_t      _suggestDifficultyId = 0;
    uint32_t      _authorizeId = 0;
    uint32_t      _pendingSubmitIds[8] = {0};
    uint8_t       _pendingSubmitNext = 0;
    uint32_t      _lastSuggestDifficultyMs = 0;
    double        _suggestedDifficulty = 0.0;
    double        _currentDifficulty = 1.0;
    StratumJob    _currentJob;

    JobCallback     _jobCb;
    SubmitCallback  _submitCb;
    DiffCallback    _diffCb;
};

} // namespace WroomMiner

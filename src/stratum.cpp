// ============================================================
//  stratum.cpp - Stratum v1 client implementation
// ============================================================
#include "stratum.h"
#include <WiFi.h>

namespace WroomMiner {

static constexpr uint32_t kSuggestDifficultyKeepaliveMs = 30000;

// Helper: hex string -> bytes
static void hexToBytes(const char* hex, uint8_t* out, size_t outLen) {
    for (size_t i = 0; i < outLen; ++i) {
        char hi = hex[i*2], lo = hex[i*2+1];
        auto hv = [](char c) -> uint8_t {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return 0;
        };
        out[i] = (hv(hi) << 4) | hv(lo);
    }
}

StratumClient::StratumClient() {}

StratumClient::~StratumClient() {
    disconnect();
}

bool StratumClient::connect(const String& host, uint16_t port,
                            const String& wallet, const String& worker,
                            const String& password,
                            double suggestedDifficulty) {
    log_i("Stratum: connecting to %s:%u", host.c_str(), port);

    IPAddress resolvedIp;
    bool resolved = WiFi.hostByName(host.c_str(), resolvedIp);
    if (resolved) {
        Serial.printf("[I/WM] Resolving pool address [%s] -> [%s]\r\n",
                      host.c_str(),
                      resolvedIp.toString().c_str());
    } else {
        Serial.printf("[W/WM] DNS failed for pool address [%s]\r\n", host.c_str());
    }

    bool connected = resolved
        ? _client.connect(resolvedIp, port, 10000)
        : _client.connect(host.c_str(), port, 10000);
    if (!connected) {
        Serial.printf("[E/WM] Failed to connect to pool [%s:%u]\r\n", host.c_str(), port);
        log_w("Stratum: connect failed");
        return false;
    }
    Serial.printf("[I/WM] Connected to pool [%s:%u]\r\n", host.c_str(), port);

    _client.setNoDelay(true);
    _rxBuffer = "";
    _connectedHost = host;
    _connectedPort = port;
    _msgId = 1;
    _subscribeId = _msgId++;
    _suggestedDifficulty = suggestedDifficulty;
    _lastSuggestDifficultyMs = 0;

    // 1. Subscribe
    Serial.println("[I/WM] Sending mining.subscribe (requesting job stream)...");
    String subscribe = String("{\"id\":") + _subscribeId +
                       ",\"method\":\"mining.subscribe\",\"params\":[\"" +
                       WROOMMINER_NAME + "\"]}\n";
    if (!sendJson(subscribe)) {
        disconnect();
        return false;
    }

    // Wait for the subscribe response (extranonce1, extranonce2_size)
    uint32_t deadline = millis() + 5000;
    while (millis() < deadline && _currentJob.extranonce1.length() == 0) {
        loop();
        delay(10);
    }

    if (_currentJob.extranonce1.length() == 0) {
        Serial.println("[E/WM] Failed to read mining.subscribe response");
        log_w("Stratum: subscribe timeout");
        disconnect();
        return false;
    }

    // 2. Authorize
    String fullWorker = wallet;
    if (worker.length() > 0) {
        fullWorker += "." + worker;
    }
    _authorizedWorker = fullWorker;
    _authorizeId = _msgId++;

    Serial.printf("[I/WM] Sending mining.authorize : %s\r\n", fullWorker.c_str());
    String authorize = String("{\"id\":") + _authorizeId +
                       ",\"method\":\"mining.authorize\",\"params\":[\"" +
                       fullWorker + "\",\"" + password + "\"]}\n";
    if (!sendJson(authorize)) {
        disconnect();
        return false;
    }

    // 3. Suggest a low share difficulty for ESP32-class miners. The pool may
    // still override this later via mining.set_difficulty.
    if (_suggestedDifficulty > 0.0 && !suggestDifficulty()) {
        disconnect();
        return false;
    }

    log_i("Stratum: connected & authorized as %s", fullWorker.c_str());
    return true;
}

void StratumClient::disconnect() {
    if (_client.connected()) {
        _client.stop();
    }
    _currentJob.valid = false;
    _currentJob.extranonce1 = "";
    _authorizedWorker = "";
    _connectedHost = "";
    _connectedPort = 0;
    _subscribeId = 0;
    _suggestDifficultyId = 0;
    _authorizeId = 0;
    _pendingSubmitId = 0;
    _lastSuggestDifficultyMs = 0;
    _suggestedDifficulty = 0.0;
}

bool StratumClient::sendJson(const String& json) {
    if (!_client.connected()) return false;
    size_t written = _client.print(json);
    return written == json.length();
}

bool StratumClient::suggestDifficulty() {
    if (_suggestedDifficulty <= 0.0) return true;

    _suggestDifficultyId = _msgId++;
    String suggest = String("{\"id\":") + _suggestDifficultyId +
                     ",\"method\":\"mining.suggest_difficulty\",\"params\":[" +
                     String(_suggestedDifficulty, 8) + "]}\n";
    Serial.printf("[I/WM] Suggesting pool difficulty : %.8f\r\n", _suggestedDifficulty);
    if (!sendJson(suggest)) return false;

    _lastSuggestDifficultyMs = millis();
    return true;
}

void StratumClient::loop() {
    if (!_client.connected()) return;

    uint32_t now = millis();
    if (_suggestedDifficulty > 0.0 &&
        (now - _lastSuggestDifficultyMs >= kSuggestDifficultyKeepaliveMs)) {
        suggestDifficulty();
    }

    while (_client.available()) {
        char c = _client.read();
        if (c == '\n') {
            if (_rxBuffer.length() > 0) {
                processLine(_rxBuffer);
                _rxBuffer = "";
            }
        } else if (c != '\r') {
            _rxBuffer += c;
            if (_rxBuffer.length() > 8192) {
                log_w("Stratum: RX buffer overflow, resetting");
                _rxBuffer = "";
            }
        }
    }
}

void StratumClient::processLine(const String& line) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, line);
    if (err) {
        log_w("Stratum: JSON parse error: %s", err.c_str());
        return;
    }

    // Notification from the server (id=null, method set)
    if (doc["method"].is<const char*>()) {
        const char* method = doc["method"];
        JsonArrayConst params = doc["params"].as<JsonArrayConst>();

        if (strcmp(method, "mining.notify") == 0) {
            handleNotify(params);
        } else if (strcmp(method, "mining.set_difficulty") == 0) {
            handleSetDifficulty(params);
        } else {
            log_d("Stratum: unhandled method %s", method);
        }
    }
    // Response to one of our requests
    else if (!doc["id"].isNull()) {
        uint32_t id = doc["id"].as<uint32_t>();

        // Subscribe response: result = [[..notification handlers..], extranonce1, extranonce2_size]
        if (id == _subscribeId && doc["result"].is<JsonArrayConst>() && _currentJob.extranonce1.length() == 0) {
            JsonArrayConst result = doc["result"].as<JsonArrayConst>();
            if (result.size() >= 3) {
                _currentJob.extranonce1     = String((const char*)result[1]);
                _currentJob.extranonce2Size = result[2].as<uint8_t>();
                Serial.printf("[I/WM] mining.subscribe successful, extranonce1=%s, en2_size=%u\r\n",
                              _currentJob.extranonce1.c_str(),
                              _currentJob.extranonce2Size);
                log_i("Stratum: extranonce1=%s en2_size=%u",
                      _currentJob.extranonce1.c_str(),
                      _currentJob.extranonce2Size);
            }
        }
        // Authorize response: this is not a share result.
        else if (id == _authorizeId) {
            bool authorized = doc["result"].is<bool>() && doc["result"].as<bool>();
            if (authorized) {
                Serial.println("[I/WM] mining.authorize accepted");
                log_i("Stratum: worker authorized");
            } else {
                Serial.println("[W/WM] mining.authorize rejected");
                log_w("Stratum: authorize failed");
            }
        }
        // Suggested difficulty response: pools may accept, ignore or reject this hint.
        else if (id == _suggestDifficultyId) {
            if (!doc["error"].isNull()) {
                Serial.printf("[W/WM] Suggest difficulty rejected: %s\r\n",
                              doc["error"].as<String>().c_str());
            } else {
                Serial.println("[I/WM] Suggest difficulty acknowledged");
            }
        }
        // Submit response
        else if (id == _pendingSubmitId && (doc["result"].is<bool>() || !doc["error"].isNull())) {
            _pendingSubmitId = 0;
            handleSubmitResponse(doc["id"], doc["result"], doc["error"]);
        }
    }
}

void StratumClient::handleNotify(JsonArrayConst params) {
    if (params.size() < 9) {
        log_w("Stratum: malformed notify (size=%u)", params.size());
        return;
    }

    StratumJob job;
    job.extranonce1     = _currentJob.extranonce1;
    job.extranonce2Size = _currentJob.extranonce2Size;
    job.difficulty      = _currentDifficulty;

    job.jobId      = String((const char*)params[0]);
    const char* prevHashHex = params[1];
    hexToBytes(prevHashHex, job.prevHash, 32);

    job.coinbase1  = String((const char*)params[2]);
    job.coinbase2  = String((const char*)params[3]);

    JsonArrayConst branches = params[4].as<JsonArrayConst>();
    job.merkleCount = 0;
    for (JsonVariantConst b : branches) {
        if (job.merkleCount >= 16) break;
        job.merkleBranches[job.merkleCount++] = String((const char*)b);
    }

    job.version = strtoul((const char*)params[5], nullptr, 16);
    job.nBits   = strtoul((const char*)params[6], nullptr, 16);
    job.nTime   = strtoul((const char*)params[7], nullptr, 16);
    job.cleanJobs = params[8].as<bool>();
    job.valid = true;

    _currentJob = job;
    Serial.printf("[L/WM] Job [%s] from [%s:%u]\r\n",
                  job.jobId.c_str(),
                  _connectedHost.c_str(),
                  _connectedPort);
    if (job.cleanJobs) {
        Serial.println("[L/WM] Job clean");
    }
    log_i("Stratum: new job %s clean=%d", job.jobId.c_str(), job.cleanJobs);

    if (_jobCb) _jobCb(job);
}

void StratumClient::handleSetDifficulty(JsonArrayConst params) {
    if (params.size() < 1) return;
    _currentDifficulty = params[0].as<double>();
    Serial.printf("[I/WM] Pool difficulty set : %.6f\r\n", _currentDifficulty);
    log_i("Stratum: difficulty set to %.6f", _currentDifficulty);
    if (_diffCb) _diffCb(_currentDifficulty);
}

void StratumClient::handleSubmitResponse(JsonVariantConst /*id*/,
                                         JsonVariantConst result,
                                         JsonVariantConst error) {
    bool accepted = result.is<bool>() && result.as<bool>();
    if (!accepted && !error.isNull()) {
        log_w("Stratum: share rejected: %s", error.as<String>().c_str());
    } else if (accepted) {
        log_i("Stratum: share accepted (diff=%.6f)", _currentDifficulty);
    }
    if (_submitCb) _submitCb(accepted, _currentDifficulty);
}

bool StratumClient::submitShare(const String& jobId,
                                const String& extranonce2,
                                uint32_t nTime,
                                uint32_t nonce) {
    char buf[256];
    _pendingSubmitId = _msgId++;
    snprintf(buf, sizeof(buf),
             "{\"id\":%u,\"method\":\"mining.submit\",\"params\":"
             "[\"%s\",\"%s\",\"%s\",\"%08x\",\"%08x\"]}\n",
             _pendingSubmitId, _authorizedWorker.c_str(), jobId.c_str(), extranonce2.c_str(), nTime, nonce);
    return sendJson(String(buf));
}

} // namespace WroomMiner

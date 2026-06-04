// ============================================================
//  config.h - Persistent configuration in NVS
// ============================================================
#pragma once

#include <Arduino.h>

namespace WroomMiner {

struct MinerConfig {
    // --- Pool ---
    String poolPrimaryUrl     = "public-pool.io";
    uint16_t poolPrimaryPort  = 21496;
    String poolFallbackUrl    = "solo.ckpool.org";
    uint16_t poolFallbackPort = 3333;
    String poolPassword       = "x";

    // --- Wallet / worker ---
    String walletAddress      = "";
    String workerName         = "wroom01";

    // --- WiFi (managed by WiFiManager, copy stored here) ---
    String wifiSsid           = "";

    // --- Behavior ---
    bool   ledEnabled         = true;
    uint8_t udpBroadcastSec   = 5;     // 0 = off
    uint16_t apiPort          = 80;
};

class Config {
public:
    // Loads configuration from NVS, returns defaults if empty.
    static bool load(MinerConfig& cfg);

    // Saves configuration to NVS.
    static bool save(const MinerConfig& cfg);

    // Clears all NVS entries (factory reset).
    static bool factoryReset();

    // Read helper: checks whether a valid configuration is stored.
    static bool hasValidConfig();
};

} // namespace WroomMiner

// ============================================================
//  config.cpp - NVS implementation via Preferences
// ============================================================
#include "config.h"
#include <Arduino.h>
#include <Preferences.h>

namespace WroomMiner {

static const char* NVS_NAMESPACE = "wroommnr";

bool Config::load(MinerConfig& cfg) {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, true)) { // read-only
        return false;
    }

    cfg.poolPrimaryUrl    = prefs.getString("ppool",     cfg.poolPrimaryUrl);
    cfg.poolPrimaryPort   = prefs.getUShort("pport",     cfg.poolPrimaryPort);
    cfg.poolFallbackUrl   = prefs.getString("fpool",     cfg.poolFallbackUrl);
    cfg.poolFallbackPort  = prefs.getUShort("fport",     cfg.poolFallbackPort);
    String legacyPassword = prefs.isKey("ppass")
        ? prefs.getString("ppass", cfg.poolPrimaryPassword)
        : cfg.poolPrimaryPassword;
    cfg.poolPrimaryPassword = prefs.isKey("ppass1")
        ? prefs.getString("ppass1", legacyPassword)
        : legacyPassword;
    cfg.poolPrimarySuggestDiff = prefs.getDouble("pdiff1", cfg.poolPrimarySuggestDiff);
    cfg.poolPrimaryMinSubmitDiff = prefs.getDouble("pmin1", cfg.poolPrimaryMinSubmitDiff);
    cfg.poolFallbackPassword = prefs.isKey("ppass2")
        ? prefs.getString("ppass2", legacyPassword)
        : legacyPassword;
    cfg.poolFallbackSuggestDiff = prefs.getDouble("pdiff2", cfg.poolFallbackSuggestDiff);
    cfg.poolFallbackMinSubmitDiff = prefs.getDouble("pmin2", cfg.poolFallbackMinSubmitDiff);
    cfg.walletAddress     = prefs.getString("wallet",    cfg.walletAddress);
    cfg.walletFallbackAddress = prefs.getString("fwallet", cfg.walletFallbackAddress);
    cfg.workerName        = prefs.getString("worker",    cfg.workerName);
    cfg.wifiSsid          = prefs.getString("ssid",      cfg.wifiSsid);
    cfg.ledEnabled        = prefs.getBool  ("led",       cfg.ledEnabled);
    cfg.udpBroadcastSec   = prefs.getUChar ("udpsec",    cfg.udpBroadcastSec);
    cfg.apiPort           = prefs.getUShort("apiport",   cfg.apiPort);

    prefs.end();

    // SparkMiner and public-pool.io docs use 21496 for ESP32-class solo miners.
    // Older WroomMiner configs may still have the generic Stratum port 3333,
    // which has shown unreliable subscribe/LowDiff behavior for this device.
    if (cfg.poolPrimaryUrl == "public-pool.io" && cfg.poolPrimaryPort == 3333) {
        Serial.println("Config: remapping public-pool.io primary port 3333 -> 21496");
        cfg.poolPrimaryPort = 21496;
    }

    return true;
}

bool Config::save(const MinerConfig& cfg) {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) { // read/write
        return false;
    }

    prefs.putString("ppool",   cfg.poolPrimaryUrl);
    prefs.putUShort("pport",   cfg.poolPrimaryPort);
    prefs.putString("fpool",   cfg.poolFallbackUrl);
    prefs.putUShort("fport",   cfg.poolFallbackPort);
    prefs.putString("ppass1",  cfg.poolPrimaryPassword);
    prefs.putString("ppass2",  cfg.poolFallbackPassword);
    prefs.putDouble("pdiff1",  cfg.poolPrimarySuggestDiff);
    prefs.putDouble("pdiff2",  cfg.poolFallbackSuggestDiff);
    prefs.putDouble("pmin1",   cfg.poolPrimaryMinSubmitDiff);
    prefs.putDouble("pmin2",   cfg.poolFallbackMinSubmitDiff);
    prefs.putString("wallet",  cfg.walletAddress);
    prefs.putString("fwallet", cfg.walletFallbackAddress);
    prefs.putString("worker",  cfg.workerName);
    prefs.putString("ssid",    cfg.wifiSsid);
    prefs.putBool  ("led",     cfg.ledEnabled);
    prefs.putUChar ("udpsec",  cfg.udpBroadcastSec);
    prefs.putUShort("apiport", cfg.apiPort);

    prefs.end();
    return true;
}

bool Config::factoryReset() {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) return false;
    bool ok = prefs.clear();
    prefs.end();
    return ok;
}

bool Config::hasValidConfig() {
    MinerConfig cfg;
    if (!load(cfg)) return false;
    return cfg.walletAddress.length() > 0;
}

} // namespace WroomMiner

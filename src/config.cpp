// ============================================================
//  config.cpp - NVS implementation via Preferences
// ============================================================
#include "config.h"
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
    cfg.poolFallbackPassword = prefs.isKey("ppass2")
        ? prefs.getString("ppass2", legacyPassword)
        : legacyPassword;
    cfg.poolFallbackSuggestDiff = prefs.getDouble("pdiff2", cfg.poolFallbackSuggestDiff);
    cfg.walletAddress     = prefs.getString("wallet",    cfg.walletAddress);
    cfg.workerName        = prefs.getString("worker",    cfg.workerName);
    cfg.wifiSsid          = prefs.getString("ssid",      cfg.wifiSsid);
    cfg.ledEnabled        = prefs.getBool  ("led",       cfg.ledEnabled);
    cfg.udpBroadcastSec   = prefs.getUChar ("udpsec",    cfg.udpBroadcastSec);
    cfg.apiPort           = prefs.getUShort("apiport",   cfg.apiPort);

    prefs.end();
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
    prefs.putString("wallet",  cfg.walletAddress);
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

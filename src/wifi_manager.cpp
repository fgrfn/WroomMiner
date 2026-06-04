// ============================================================
//  wifi_manager.cpp - WiFi with captive portal
//  Uses tzapu's WiFiManager for the AP config setup.
// ============================================================
#include "wifi_manager.h"
#include <WiFi.h>
#include <WiFiManager.h>

namespace WroomMiner {

bool WifiSetup::connectOrConfig(MinerConfig& cfg) {
    // Custom parameters for pool / wallet
    WiFiManager wm;
    wm.setConfigPortalTimeout(180);

    WiFiManagerParameter pPoolUrl ("ppool",  "Pool URL (primary)",
                                    cfg.poolPrimaryUrl.c_str(), 64);
    char portBuf[8]; snprintf(portBuf, sizeof(portBuf), "%u", cfg.poolPrimaryPort);
    WiFiManagerParameter pPoolPort("pport",  "Pool Port", portBuf, 6);
    WiFiManagerParameter pPoolPass("ppass1", "Pool Password",
                                    cfg.poolPrimaryPassword.c_str(), 32);
    char diffBuf[12]; snprintf(diffBuf, sizeof(diffBuf), "%.6f", cfg.poolPrimarySuggestDiff);
    WiFiManagerParameter pPoolDiff("pdiff1", "Suggested Diff", diffBuf, 10);
    WiFiManagerParameter pWallet  ("wallet", "BTC Wallet Address",
                                    cfg.walletAddress.c_str(), 100);
    WiFiManagerParameter pWorker  ("worker", "Worker Name",
                                    cfg.workerName.c_str(), 24);

    wm.addParameter(&pPoolUrl);
    wm.addParameter(&pPoolPort);
    wm.addParameter(&pPoolPass);
    wm.addParameter(&pPoolDiff);
    wm.addParameter(&pWallet);
    wm.addParameter(&pWorker);

    // autoConnect: try first, then captive portal
    bool ok = wm.autoConnect("WroomMiner-Setup");
    if (!ok) {
        log_e("WiFi: connect/config failed");
        return false;
    }

    // Apply values from the portal
    cfg.poolPrimaryUrl  = pPoolUrl.getValue();
    cfg.poolPrimaryPort = atoi(pPoolPort.getValue());
    cfg.poolPrimaryPassword = pPoolPass.getValue();
    cfg.poolPrimarySuggestDiff = atof(pPoolDiff.getValue());
    cfg.walletAddress   = pWallet.getValue();
    cfg.workerName      = pWorker.getValue();
    cfg.wifiSsid        = WiFi.SSID();

    Config::save(cfg);

    log_i("WiFi: connected to %s, IP=%s",
          cfg.wifiSsid.c_str(), WiFi.localIP().toString().c_str());
    return true;
}

void WifiSetup::startConfigPortal(MinerConfig& cfg) {
    WiFiManager wm;
    wm.setConfigPortalTimeout(300);
    wm.startConfigPortal("WroomMiner-Setup");
    cfg.wifiSsid = WiFi.SSID();
    Config::save(cfg);
}

bool WifiSetup::isConnected() {
    return WiFi.status() == WL_CONNECTED;
}

String WifiSetup::localIp() {
    return WiFi.localIP().toString();
}

int WifiSetup::rssi() {
    return WiFi.RSSI();
}

} // namespace WroomMiner

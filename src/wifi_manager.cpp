// ============================================================
//  wifi_manager.cpp - WiFi with captive portal
//  Uses tzapu's WiFiManager for the AP config setup.
// ============================================================
#include "wifi_manager.h"
#include <WiFi.h>
#include <WiFiManager.h>
#include <esp_wifi.h>

namespace WroomMiner {

namespace {

static uint32_t g_lastReconnectAttemptMs = 0;
static uint32_t g_disconnectedSinceMs = 0;
static wl_status_t g_lastWifiStatus = WL_IDLE_STATUS;

} // namespace

void WifiSetup::applyRadioTuning() {
    WiFi.mode(WIFI_STA);
    WiFi.persistent(false);
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(true);

    // ESP-IDF uses quarter-dBm units; 78 = 19.5 dBm, the ESP32 max.
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_set_max_tx_power(78);
}

bool WifiSetup::connectOrConfig(MinerConfig& cfg) {
    // Custom parameters for pool / wallet
    applyRadioTuning();

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
    char minDiffBuf[12]; snprintf(minDiffBuf, sizeof(minDiffBuf), "%.6f", cfg.poolPrimaryMinSubmitDiff);
    WiFiManagerParameter pPoolMinDiff("pmin1", "Min Submit Diff", minDiffBuf, 10);
    WiFiManagerParameter pWallet  ("wallet", "BTC Wallet Address",
                                    cfg.walletAddress.c_str(), 100);
    WiFiManagerParameter pFallbackWallet("fwallet", "Fallback BTC Wallet",
                                    cfg.walletFallbackAddress.c_str(), 100);
    WiFiManagerParameter pWorker  ("worker", "Worker Name",
                                    cfg.workerName.c_str(), 24);

    wm.addParameter(&pPoolUrl);
    wm.addParameter(&pPoolPort);
    wm.addParameter(&pPoolPass);
    wm.addParameter(&pPoolDiff);
    wm.addParameter(&pPoolMinDiff);
    wm.addParameter(&pWallet);
    wm.addParameter(&pFallbackWallet);
    wm.addParameter(&pWorker);

    // autoConnect: try first, then captive portal
    bool ok = wm.autoConnect("WroomMiner-Setup");
    if (!ok) {
        log_e("WiFi: connect/config failed");
        return false;
    }
    applyRadioTuning();

    // Apply values from the portal
    cfg.poolPrimaryUrl  = pPoolUrl.getValue();
    cfg.poolPrimaryPort = atoi(pPoolPort.getValue());
    cfg.poolPrimaryPassword = pPoolPass.getValue();
    cfg.poolPrimarySuggestDiff = atof(pPoolDiff.getValue());
    cfg.poolPrimaryMinSubmitDiff = atof(pPoolMinDiff.getValue());
    cfg.walletAddress   = pWallet.getValue();
    cfg.walletFallbackAddress = pFallbackWallet.getValue();
    cfg.workerName      = pWorker.getValue();
    cfg.wifiSsid        = WiFi.SSID();

    Config::save(cfg);

    log_i("WiFi: connected to %s, IP=%s",
          cfg.wifiSsid.c_str(), WiFi.localIP().toString().c_str());
    return true;
}

void WifiSetup::startConfigPortal(MinerConfig& cfg) {
    applyRadioTuning();

    WiFiManager wm;
    wm.setConfigPortalTimeout(300);
    wm.startConfigPortal("WroomMiner-Setup");
    applyRadioTuning();
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

void WifiSetup::maintain() {
    wl_status_t status = WiFi.status();
    if (status == WL_CONNECTED) {
        g_disconnectedSinceMs = 0;
        g_lastWifiStatus = status;
        return;
    }

    uint32_t now = millis();
    if (g_disconnectedSinceMs == 0) {
        g_disconnectedSinceMs = now;
        Serial.printf("WiFi disconnected, status=%d; waiting for auto reconnect...\r\n",
                      static_cast<int>(status));
    }

    if (status == WL_IDLE_STATUS || status == WL_SCAN_COMPLETED) {
        g_lastWifiStatus = status;
        return;
    }

    // WiFi.setAutoReconnect(true) already asks the driver to reconnect. Only
    // nudge it if it has stayed down for a while; do not force disconnect(),
    // because that can interrupt an in-flight driver reconnect.
    if (now - g_disconnectedSinceMs < 60000 ||
        now - g_lastReconnectAttemptMs < 60000) {
        g_lastWifiStatus = status;
        return;
    }
    g_lastReconnectAttemptMs = now;

    if (status != g_lastWifiStatus) {
        Serial.printf("WiFi status changed: %d\r\n", static_cast<int>(status));
    }
    Serial.println("WiFi still disconnected, nudging reconnect...");
    applyRadioTuning();
    WiFi.reconnect();
    g_lastWifiStatus = status;
}

} // namespace WroomMiner

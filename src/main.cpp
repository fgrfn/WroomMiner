// ============================================================
//  main.cpp - WroomMiner entry point
//
//  Boot sequence:
//    1. Load NVS configuration
//    2. Connect WiFi (or start captive portal)
//    3. Start API server (Core 0)
//    4. Connect Stratum client
//    5. Start mining task (Core 1)
//
//  In loop():
//    - Process Stratum messages
//    - Tick stats
//    - WebSocket broadcasts
//    - Reconnect logic
// ============================================================
#include <Arduino.h>
#include "config.h"
#include "stats.h"
#include "wifi_manager.h"
#include "stratum.h"
#include "mining.h"
#include "api_server.h"

using namespace WroomMiner;

// Globals
static MinerConfig   g_config;
static StratumClient g_stratum;
static MiningEngine  g_mining;
static ApiServer     g_api;

// Reconnect backoff
static uint32_t g_lastConnectAttempt = 0;
static uint32_t g_reconnectDelayMs   = 5000;

// ============================================================
//  Establish / re-establish the pool connection
// ============================================================
static bool connectToPool(bool useFallback = false) {
    String host = useFallback ? g_config.poolFallbackUrl : g_config.poolPrimaryUrl;
    uint16_t port = useFallback ? g_config.poolFallbackPort : g_config.poolPrimaryPort;

    log_i("Pool: trying %s:%u", host.c_str(), port);

    bool ok = g_stratum.connect(host, port,
                                g_config.walletAddress,
                                g_config.workerName,
                                g_config.poolPassword);

    if (ok) {
        log_i("Pool: connected");
        g_reconnectDelayMs = 5000; // reset backoff
    } else {
        log_w("Pool: connect failed, will retry");
        g_reconnectDelayMs = min<uint32_t>(g_reconnectDelayMs * 2, 60000);
    }

    return ok;
}

// ============================================================
//  Setup
// ============================================================
void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println();
    Serial.println("==============================================");
    Serial.println("  " WROOMMINER_NAME " " WROOMMINER_VERSION);
    Serial.println("  Build: " __DATE__ " " __TIME__);
    Serial.println("==============================================");

    // 1. Load configuration
    Config::load(g_config);
    log_i("Boot: config loaded (wallet=%s, pool=%s:%u)",
          g_config.walletAddress.length() > 0 ? "set" : "EMPTY",
          g_config.poolPrimaryUrl.c_str(),
          g_config.poolPrimaryPort);

    // 2. Restore stats
    Stats::restore();

    // 3. Connect WiFi (or captive portal)
    if (!WifiSetup::connectOrConfig(g_config)) {
        log_e("Boot: WiFi setup failed, restarting in 10s");
        delay(10000);
        ESP.restart();
    }

    // 4. Start the API server
    g_api.start(&g_config);

    // 5. Wire up Stratum callbacks
    g_stratum.onJob([](const StratumJob& job) {
        g_mining.onNewJob(job);
    });
    g_stratum.onSubmit([](bool accepted, double diff) {
        if (accepted) {
            Stats::recordShareAccepted(diff);
            Stats::persist();
        } else {
            Stats::recordShareRejected();
        }
    });

    // 6. Start the mining task (even without a pool connection - it waits for jobs)
    g_mining.start(&g_stratum);

    // 7. Attempt the first pool connection
    if (g_config.walletAddress.length() > 0) {
        connectToPool(false);
    } else {
        log_w("Boot: no wallet configured - mining will not start");
    }

    log_i("Boot: setup complete");
}

// ============================================================
//  Main loop (Core 0)
// ============================================================
void loop() {
    // Process incoming Stratum messages
    g_stratum.loop();

    // Tick stats (hashrate average)
    static uint32_t lastTick = 0;
    uint32_t now = millis();
    if (now - lastTick >= 1000) {
        Stats::tick();
        lastTick = now;
    }

    // WebSocket broadcasts
    g_api.tick();

    // Pool reconnect logic
    if (!g_stratum.isConnected() &&
        g_config.walletAddress.length() > 0 &&
        WifiSetup::isConnected() &&
        (now - g_lastConnectAttempt > g_reconnectDelayMs)) {

        g_lastConnectAttempt = now;
        if (!connectToPool(false)) {
            // Primary failed - try the fallback
            log_w("Pool: primary failed, trying fallback");
            connectToPool(true);
        }
    }

    // Yield - other tasks (especially WiFi) need time
    delay(10);
}

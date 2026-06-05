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
#include "discovery.h"
#include "sha256d.h"

using namespace WroomMiner;

// Globals
static MinerConfig   g_config;
static StratumClient g_stratum;
static MiningEngine  g_mining;
static ApiServer     g_api;
static DiscoveryBroadcaster g_discovery;
static String        g_activePoolKind = "none";
static String        g_activePoolHost = "";
static uint16_t      g_activePoolPort = 0;

// Reconnect backoff
static uint32_t g_lastConnectAttempt = 0;
static uint32_t g_reconnectDelayMs   = 5000;
static uint8_t  g_primaryFailCount   = 0;
static constexpr uint8_t kPrimaryRetriesBeforeFallback = 3;

static void formatHashrate(float value, char* out, size_t outSize) {
    if (value >= 1000000.0f) {
        snprintf(out, outSize, "%.4f MH/s", value / 1000000.0f);
    } else if (value >= 1000.0f) {
        snprintf(out, outSize, "%.2f KH/s", value / 1000.0f);
    } else {
        snprintf(out, outSize, "%.1f  H/s", value);
    }
}

static void formatUptime(uint32_t seconds, char* out, size_t outSize) {
    uint32_t days = seconds / 86400;
    seconds %= 86400;
    uint32_t hours = seconds / 3600;
    seconds %= 3600;
    uint32_t minutes = seconds / 60;
    seconds %= 60;
    if (days > 0) {
        snprintf(out, outSize, "%lud %02lu:%02lu:%02lu",
                 static_cast<unsigned long>(days),
                 static_cast<unsigned long>(hours),
                 static_cast<unsigned long>(minutes),
                 static_cast<unsigned long>(seconds));
    } else {
        snprintf(out, outSize, "%02lu:%02lu:%02lu",
                 static_cast<unsigned long>(hours),
                 static_cast<unsigned long>(minutes),
                 static_cast<unsigned long>(seconds));
    }
}

static void printMiningOverview() {
    // All rows are exactly 120 chars wide.
    // Layout: "# " + col1(12) + " | " + col2(12) + " | " + col3(18) + " | " +
    //         col4(10) + " | " + col5(10) + " | " + col6(13) + " | " +
    //         col7(9) + " | " + col8(11) + " #"
    // = 2 + 12 + 3 + 12 + 3 + 18 + 3 + 10 + 3 + 10 + 3 + 13 + 3 + 9 + 3 + 11 + 2 = 120
    static const char* kRule =
        "########################################################"
        "################################################################";  // 120 #
    static const char* kLabels =
        "#  hashrate 1s |  hashrate 1m | shares sub/acc/rej |  pool diff |  best diff |  total hashes |      heap |        rssi #";

    auto& s = Stats::get();

    char uptime[18];
    char hash1s[14];
    char hash1m[14];
    formatUptime(Stats::uptimeSeconds(), uptime, sizeof(uptime));
    formatHashrate(s.hashrate1s.load(), hash1s, sizeof(hash1s));
    formatHashrate(s.hashrate1m.load(), hash1m, sizeof(hash1m));

    const bool connected = g_stratum.isConnected();
    String poolStr = connected ? (g_activePoolHost + ":" + String(g_activePoolPort)) : "disconnected";

    uint32_t accepted  = s.sharesAccepted.load();
    uint32_t rejected  = s.sharesRejected.load();
    uint32_t submitted = accepted + rejected;

    // Header: "# WroomMiner <ver> || uptime <up> || active Pool: <pool><pad>#"
    // Fixed prefix length: "# WroomMiner " + ver + " || uptime " + uptime + " || active Pool: " = 2+13+ver+10+uptime+16
    char hdr[128];
    int prefixLen = 2 + 14 + (int)strlen(WROOMMINER_VERSION) + 10 + (int)strlen(uptime) + 17;
    int padLen = 119 - prefixLen - (int)poolStr.length();
    if (padLen < 0) padLen = 0;
    snprintf(hdr, sizeof(hdr), "# WroomMiner v%s || uptime %s || active Pool: %s%*s#",
             WROOMMINER_VERSION, uptime, poolStr.c_str(), padLen, "");

    // Data row — col widths match kLabels exactly
    char shares[20];
    snprintf(shares, sizeof(shares), "%lu/%lu/%lu",
             static_cast<unsigned long>(submitted),
             static_cast<unsigned long>(accepted),
             static_cast<unsigned long>(rejected));
    char data[128];
    snprintf(data, sizeof(data),
             "# %12s | %12s | %-18s | %10.6f | %10.6f | %13llu | %6lu kB | %8ddBm #",
             hash1s, hash1m,
             shares,
             connected ? g_stratum.currentDifficulty() : 0.0,
             s.bestDifficulty.load(),
             static_cast<unsigned long long>(s.hashCount.load()),
             static_cast<unsigned long>(ESP.getFreeHeap() / 1024),
             WifiSetup::isConnected() ? WifiSetup::rssi() : 0);

    Serial.printf("%s\r\n", kRule);
    Serial.printf("%s\r\n", hdr);
    Serial.printf("%s\r\n", kRule);
    Serial.printf("%s\r\n", kLabels);
    Serial.printf("%s\r\n", data);
    Serial.printf("%s\r\n", kRule);
}

// ============================================================
//  Establish / re-establish the pool connection
// ============================================================
static bool connectToPool(bool useFallback = false) {
    String host = useFallback ? g_config.poolFallbackUrl : g_config.poolPrimaryUrl;
    uint16_t port = useFallback ? g_config.poolFallbackPort : g_config.poolPrimaryPort;
    String password = useFallback ? g_config.poolFallbackPassword : g_config.poolPrimaryPassword;
    double suggestedDifficulty = useFallback ? g_config.poolFallbackSuggestDiff : g_config.poolPrimarySuggestDiff;
    double minSubmitDifficulty = useFallback ? g_config.poolFallbackMinSubmitDiff : g_config.poolPrimaryMinSubmitDiff;
    String wallet = useFallback && g_config.walletFallbackAddress.length() > 0
        ? g_config.walletFallbackAddress
        : g_config.walletAddress;

    Serial.printf("Connecting to %s pool [%s:%u]...\r\n",
                  useFallback ? "fallback" : "primary",
                  host.c_str(),
                  port);
    log_i("Pool: trying %s:%u", host.c_str(), port);
    g_mining.setMinimumShareDifficulty(minSubmitDifficulty);

    bool ok = g_stratum.connect(host, port,
                                wallet,
                                g_config.workerName,
                                password,
                                suggestedDifficulty);

    if (ok) {
        g_activePoolKind = useFallback ? "fallback" : "primary";
        g_activePoolHost = host;
        g_activePoolPort = port;
        g_api.setPoolState(g_activePoolKind, g_activePoolHost, g_activePoolPort, true);
        log_i("Pool: connected");
        g_reconnectDelayMs = 5000; // reset backoff
        g_primaryFailCount = 0;
    } else {
        if (!g_stratum.isConnected()) {
            g_api.setPoolState("none", "", 0, false);
        }
        Serial.printf("Pool connection failed, retry in %lu ms\r\n",
                      static_cast<unsigned long>(g_reconnectDelayMs));
        log_w("Pool: connect failed, will retry");
        g_primaryFailCount += !useFallback ? 1 : 0;
        g_reconnectDelayMs = min<uint32_t>(g_reconnectDelayMs * 2, 60000);
    }

    return ok;
}

// ============================================================
//  Setup
// ============================================================
void setup() {
    Serial.begin(115200);
    setCpuFrequencyMhz(240);
    delay(500);

    Serial.println();
    Serial.println("██╗    ██╗██████╗  ██████╗  ██████╗ ███╗   ███╗███╗   ███╗██╗███╗   ██╗███████╗██████╗ ");
    Serial.println("██║    ██║██╔══██╗██╔═══██╗██╔═══██╗████╗ ████║████╗ ████║██║████╗  ██║██╔════╝██╔══██╗");
    Serial.println("██║ █╗ ██║██████╔╝██║   ██║██║   ██║██╔████╔██║██╔████╔██║██║██╔██╗ ██║█████╗  ██████╔╝");
    Serial.println("██║███╗██║██╔══██╗██║   ██║██║   ██║██║╚██╔╝██║██║╚██╔╝██║██║██║╚██╗██║██╔══╝  ██╔══██╗");
    Serial.println("╚███╔███╔╝██║  ██║╚██████╔╝╚██████╔╝██║ ╚═╝ ██║██║ ╚═╝ ██║██║██║ ╚████║███████╗██║  ██║");
    Serial.println(" ╚══╝╚══╝ ╚═╝  ╚═╝ ╚═════╝  ╚═════╝ ╚═╝     ╚═╝╚═╝     ╚═╝╚═╝╚═╝  ╚═══╝╚══════╝╚═╝  ╚═╝");
    Serial.println();
    Serial.println("  v" WROOMMINER_VERSION "  |  Build: " __DATE__ " " __TIME__ "  |  ESP32-WROOM-32D");
    Serial.println("  WebFlasher: http://flash.fgrfn.de  -  Github: https://github.com/fgrfn/WroomMiner");
    Serial.println();

    // 0. Verify midstate SHA256d implementation against naive reference
    {
        // Known Bitcoin block header (80 bytes) and its SHA256d hash.
        static const uint8_t testHeader[80] = {
            0x00,0x00,0x00,0x20,0x2c,0x61,0xaf,0x3d,0xf3,0x2c,0x27,0x49,0xd5,0x2c,0x44,0xfa,
            0x8c,0xc3,0x8d,0xbc,0x57,0xad,0x8d,0x57,0xdf,0x68,0x01,0x00,0x00,0x00,0x00,0x00,
            0x00,0x00,0x00,0x00,0xae,0xd0,0x11,0x89,0x75,0x7f,0x06,0xe5,0xa0,0x88,0x77,0xa8,
            0x46,0x60,0xa9,0xb9,0x71,0x0a,0x3a,0xca,0x86,0x35,0x6f,0x6a,0x4d,0x6c,0x0b,0xe5,
            0x24,0x52,0x1e,0x29,0x72,0xf9,0x21,0x6a,0x8f,0x06,0x02,0x17,0x0b,0x3e,0x03,0x00
        };
        // SHA256d of above header (big-endian): 00000501100cad7a...
        static const uint8_t expectedHash[32] = {
            0x00,0x00,0x05,0x01,0x10,0x0c,0xad,0x7a,0x13,0x1f,0x5f,0x6d,0x2b,0xeb,0xfc,0x36,
            0x2e,0x14,0x62,0xe8,0x59,0x01,0x8c,0xd2,0x08,0xf1,0x3e,0x3f,0xcc,0xe7,0xb3,0xfc
        };

        uint8_t naiveHash[32], midstateHash[32];
        sha256d(testHeader, 80, naiveHash);

        Sha256Midstate ms;
        sha256d_prepare_midstate(testHeader, ms);
        // Nonce from the header = bytes 76-79 LE = 0x00033e0b

        sha256d_try_enable_hardware_midstate(ms, 0x00033e0bu, expectedHash);
        bool midstateRan = sha256d_with_midstate(ms, 0x00033e0bu, midstateHash);

        bool naiveOk = memcmp(naiveHash, expectedHash, 32) == 0;
        bool midstateOk = midstateRan && memcmp(midstateHash, expectedHash, 32) == 0;
        if (!midstateOk) {
            sha256d_set_backend(Sha256dBackend::SoftwareMidstate);
            midstateRan = sha256d_with_midstate(ms, 0x00033e0bu, midstateHash);
            midstateOk = midstateRan && memcmp(midstateHash, expectedHash, 32) == 0;
        }

        Serial.printf("  SHA256d self-test: naive=%s  midstate=%s\r\n",
                      naiveOk    ? "PASS" : "FAIL",
                      midstateOk ? "PASS" : "FAIL");
        Serial.printf("  SHA256d backend: %s\r\n", sha256d_backend_name());
        if (!naiveOk || !midstateOk) {
            Serial.println("  WARNING: SHA256d self-test FAILED - mining results will be wrong!");
        }
    }

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
    g_discovery.start(&g_config);

    // 5. Wire up Stratum callbacks
    g_stratum.onJob([](const StratumJob& job) {
        g_mining.onNewJob(job);
    });
    g_stratum.onDifficulty([](double difficulty) {
        g_mining.onDifficulty(difficulty);
    });
    g_stratum.onSubmit([](bool accepted, double diff) {
        if (accepted) {
            Stats::recordShareAccepted(diff);
            Stats::persist();
            auto& s = Stats::get();
            Serial.printf("SHARE ACCEPTED  #%lu  diff=%.6f\r\n",
                          static_cast<unsigned long>(s.sharesAccepted.load()),
                          diff);
        } else {
            Stats::recordShareRejected();
            auto& s = Stats::get();
            Serial.printf("SHARE REJECTED  #%lu total rejected\r\n",
                          static_cast<unsigned long>(s.sharesRejected.load()));
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
    WifiSetup::maintain();

    // Process incoming Stratum messages
    g_stratum.loop();

    // Tick stats (hashrate average)
    static uint32_t lastTick = 0;
    uint32_t now = millis();
    if (now - lastTick >= 1000) {
        Stats::tick();
        lastTick = now;
    }

    static uint32_t lastSerialStatus = 0;
    if (now - lastSerialStatus >= 30000) {
        lastSerialStatus = now;
        printMiningOverview();
    }

    // WebSocket broadcasts
    g_api.tick();
    g_discovery.tick();

    // Pool reconnect logic
    if (!g_stratum.isConnected() &&
        g_config.walletAddress.length() > 0 &&
        WifiSetup::isConnected() &&
        (now - g_lastConnectAttempt > g_reconnectDelayMs)) {

        g_lastConnectAttempt = now;
        bool useFallback = g_primaryFailCount >= kPrimaryRetriesBeforeFallback;
        if (!connectToPool(useFallback) && useFallback) {
            // Fallback also failed - reset counter so we retry primary next
            g_primaryFailCount = 0;
        }
    }

    // Yield - other tasks (especially WiFi) need time
    delay(10);
}

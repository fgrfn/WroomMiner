// ============================================================
//  api_server.cpp - REST API + WebSocket implementation
// ============================================================
#include "api_server.h"
#include "stats.h"
#include <WiFi.h>
#include <esp_system.h>
#include <esp_chip_info.h>
#include <esp_idf_version.h>
#include <ArduinoJson.h>
#include <AsyncJson.h>

namespace WroomMiner {

ApiServer::ApiServer() {}

void ApiServer::start(MinerConfig* cfg) {
    _cfg = cfg;
    _server = new AsyncWebServer(cfg->apiPort);
    _ws     = new AsyncWebSocket("/ws");

    _ws->onEvent([this](AsyncWebSocket* s, AsyncWebSocketClient* c,
                        AwsEventType t, void* arg, uint8_t* d, size_t l) {
        onWsEvent(s, c, t, arg, d, l);
    });
    _server->addHandler(_ws);

    setupRoutes();
    _server->begin();
    log_i("API: server started on port %u", cfg->apiPort);
}

void ApiServer::stop() {
    if (_server) {
        _server->end();
        delete _server;
        _server = nullptr;
    }
    if (_ws) {
        delete _ws;
        _ws = nullptr;
    }
}

void ApiServer::setupRoutes() {
    // --- Root ---
    _server->on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(200, "text/html",
            "<h1>WroomMiner</h1>"
            "<p>API: <a href='/api/v1/status'>/api/v1/status</a></p>"
            "<p>Version " WROOMMINER_VERSION "</p>");
    });

    // --- GET endpoints ---
    _server->on("/api/v1/status",  HTTP_GET, [this](AsyncWebServerRequest* r){ handleStatus(r); });
    _server->on("/api/v1/mining",  HTTP_GET, [this](AsyncWebServerRequest* r){ handleMining(r); });
    _server->on("/api/v1/pool",    HTTP_GET, [this](AsyncWebServerRequest* r){ handlePool(r); });
    _server->on("/api/v1/network", HTTP_GET, [this](AsyncWebServerRequest* r){ handleNetwork(r); });
    _server->on("/api/v1/system",  HTTP_GET, [this](AsyncWebServerRequest* r){ handleSystem(r); });
    _server->on("/api/v1/config",  HTTP_GET, [this](AsyncWebServerRequest* r){ handleConfigGet(r); });
    _server->on("/api/v1/stats",   HTTP_GET, [this](AsyncWebServerRequest* r){ handleStats(r); });
    _server->on("/api/v1/info",    HTTP_GET, [this](AsyncWebServerRequest* r){ handleInfo(r); });

    // --- POST endpoints ---
    auto* configPost = new AsyncCallbackJsonWebHandler("/api/v1/config",
        [this](AsyncWebServerRequest* r, JsonVariant& body) { handleConfigPost(r, body); });
    _server->addHandler(configPost);

    _server->on("/api/v1/action/restart", HTTP_POST,
                [this](AsyncWebServerRequest* r){ handleRestart(r); });
    _server->on("/api/v1/action/reset",   HTTP_POST,
                [this](AsyncWebServerRequest* r){ handleFactoryReset(r); });

    // 404
    _server->onNotFound([](AsyncWebServerRequest* req) {
        req->send(404, "application/json", "{\"error\":\"not_found\"}");
    });
}

// ============================================================
//  GET handlers
// ============================================================

void ApiServer::handleStatus(AsyncWebServerRequest* req) {
    AsyncJsonResponse* res = new AsyncJsonResponse();
    JsonObject root = res->getRoot().to<JsonObject>();

    auto& s = Stats::get();
    root["uptime_seconds"]    = Stats::uptimeSeconds();
    root["hashrate_hs"]       = s.hashrate1m.load();
    root["shares_accepted"]   = s.sharesAccepted.load();
    root["shares_rejected"]   = s.sharesRejected.load();
    root["best_difficulty"]   = s.bestDifficulty.load();
    root["wifi_connected"]    = WiFi.status() == WL_CONNECTED;
    root["wifi_rssi"]         = WiFi.RSSI();
    root["free_heap"]         = ESP.getFreeHeap();
    root["firmware_version"]  = WROOMMINER_VERSION;

    res->setLength();
    req->send(res);
}

void ApiServer::handleMining(AsyncWebServerRequest* req) {
    AsyncJsonResponse* res = new AsyncJsonResponse();
    JsonObject root = res->getRoot().to<JsonObject>();

    auto& s = Stats::get();
    root["hashrate_1s_hs"]    = s.hashrate1s.load();
    root["hashrate_1m_hs"]    = s.hashrate1m.load();
    root["total_hashes"]      = s.hashCount.load();
    root["shares_accepted"]   = s.sharesAccepted.load();
    root["shares_rejected"]   = s.sharesRejected.load();
    root["blocks_found"]      = s.blocksFound.load();
    root["best_difficulty"]   = s.bestDifficulty.load();
    root["session_best_diff"] = s.sessionBestDiff.load();
    root["uptime_seconds"]    = Stats::uptimeSeconds();
    root["last_share_ago_ms"] = s.lastShareMillis ? (millis() - s.lastShareMillis) : 0;

    res->setLength();
    req->send(res);
}

void ApiServer::handlePool(AsyncWebServerRequest* req) {
    AsyncJsonResponse* res = new AsyncJsonResponse();
    JsonObject root = res->getRoot().to<JsonObject>();

    JsonObject primary = root["primary"].to<JsonObject>();
    primary["url"]  = _cfg->poolPrimaryUrl;
    primary["port"] = _cfg->poolPrimaryPort;

    JsonObject fallback = root["fallback"].to<JsonObject>();
    fallback["url"]  = _cfg->poolFallbackUrl;
    fallback["port"] = _cfg->poolFallbackPort;

    root["active"]    = "primary"; // TODO: make dynamic in Phase 2
    root["connected"] = true;
    root["worker"]    = _cfg->workerName;

    res->setLength();
    req->send(res);
}

void ApiServer::handleNetwork(AsyncWebServerRequest* req) {
    AsyncJsonResponse* res = new AsyncJsonResponse();
    JsonObject root = res->getRoot().to<JsonObject>();

    root["ssid"]         = WiFi.SSID();
    root["rssi"]         = WiFi.RSSI();
    root["ip"]           = WiFi.localIP().toString();
    root["mac"]          = WiFi.macAddress();
    root["gateway"]      = WiFi.gatewayIP().toString();
    root["dns"]          = WiFi.dnsIP().toString();
    root["channel"]      = WiFi.channel();
    root["hostname"]     = WiFi.getHostname();

    res->setLength();
    req->send(res);
}

void ApiServer::handleSystem(AsyncWebServerRequest* req) {
    AsyncJsonResponse* res = new AsyncJsonResponse();
    JsonObject root = res->getRoot().to<JsonObject>();

    esp_chip_info_t chipInfo;
    esp_chip_info(&chipInfo);

    root["chip_model"]   = (chipInfo.model == CHIP_ESP32) ? "ESP32" :
                           (chipInfo.model == CHIP_ESP32S2) ? "ESP32-S2" :
                           (chipInfo.model == CHIP_ESP32S3) ? "ESP32-S3" :
                           (chipInfo.model == CHIP_ESP32C3) ? "ESP32-C3" : "Unknown";
    root["chip_cores"]      = chipInfo.cores;
    root["chip_revision"]   = chipInfo.revision;
    root["cpu_freq_mhz"]    = ESP.getCpuFreqMHz();
    root["flash_size"]      = ESP.getFlashChipSize();
    root["sketch_size"]     = ESP.getSketchSize();
    root["sketch_free"]     = ESP.getFreeSketchSpace();
    root["free_heap"]       = ESP.getFreeHeap();
    root["min_free_heap"]   = ESP.getMinFreeHeap();
    root["heap_size"]       = ESP.getHeapSize();
    root["idf_version"]     = esp_get_idf_version();
    root["temperature_c"]   = -1; // ESP32-WROOM-32D has no internal temp sensor
    root["uptime_seconds"]  = Stats::uptimeSeconds();

    res->setLength();
    req->send(res);
}

void ApiServer::handleConfigGet(AsyncWebServerRequest* req) {
    AsyncJsonResponse* res = new AsyncJsonResponse();
    JsonObject root = res->getRoot().to<JsonObject>();

    root["pool_primary_url"]   = _cfg->poolPrimaryUrl;
    root["pool_primary_port"]  = _cfg->poolPrimaryPort;
    root["pool_fallback_url"]  = _cfg->poolFallbackUrl;
    root["pool_fallback_port"] = _cfg->poolFallbackPort;
    root["wallet_address"]     = _cfg->walletAddress;
    root["worker_name"]        = _cfg->workerName;
    root["wifi_ssid"]          = _cfg->wifiSsid;
    root["led_enabled"]        = _cfg->ledEnabled;
    root["udp_broadcast_sec"]  = _cfg->udpBroadcastSec;
    root["api_port"]           = _cfg->apiPort;
    // pool_password intentionally NOT returned

    res->setLength();
    req->send(res);
}

void ApiServer::handleStats(AsyncWebServerRequest* req) {
    // Extended statistics with all details
    handleMining(req); // Phase 1: same as /mining
}

void ApiServer::handleInfo(AsyncWebServerRequest* req) {
    AsyncJsonResponse* res = new AsyncJsonResponse();
    JsonObject root = res->getRoot().to<JsonObject>();

    root["name"]            = WROOMMINER_NAME;
    root["version"]         = WROOMMINER_VERSION;
    root["build_date"]      = __DATE__ " " __TIME__;
    root["api_version"]     = "v1";
    root["compatible_with"] = "HashHive";

    res->setLength();
    req->send(res);
}

// ============================================================
//  POST handlers
// ============================================================

void ApiServer::handleConfigPost(AsyncWebServerRequest* req, JsonVariant& body) {
    JsonObject in = body.as<JsonObject>();
    if (in.isNull()) {
        req->send(400, "application/json", "{\"error\":\"invalid_json\"}");
        return;
    }

    bool changed = false;
    if (in["pool_primary_url"].is<const char*>()) {
        _cfg->poolPrimaryUrl = in["pool_primary_url"].as<String>(); changed = true;
    }
    if (in["pool_primary_port"].is<int>()) {
        _cfg->poolPrimaryPort = in["pool_primary_port"]; changed = true;
    }
    if (in["pool_fallback_url"].is<const char*>()) {
        _cfg->poolFallbackUrl = in["pool_fallback_url"].as<String>(); changed = true;
    }
    if (in["pool_fallback_port"].is<int>()) {
        _cfg->poolFallbackPort = in["pool_fallback_port"]; changed = true;
    }
    if (in["wallet_address"].is<const char*>()) {
        _cfg->walletAddress = in["wallet_address"].as<String>(); changed = true;
    }
    if (in["worker_name"].is<const char*>()) {
        _cfg->workerName = in["worker_name"].as<String>(); changed = true;
    }
    if (in["pool_password"].is<const char*>()) {
        _cfg->poolPassword = in["pool_password"].as<String>(); changed = true;
    }
    if (in["led_enabled"].is<bool>()) {
        _cfg->ledEnabled = in["led_enabled"]; changed = true;
    }

    if (changed) {
        Config::save(*_cfg);
        req->send(200, "application/json", "{\"status\":\"saved\",\"restart_required\":true}");
    } else {
        req->send(400, "application/json", "{\"error\":\"no_valid_fields\"}");
    }
}

void ApiServer::handleRestart(AsyncWebServerRequest* req) {
    req->send(200, "application/json", "{\"status\":\"restarting\"}");
    delay(500);
    ESP.restart();
}

void ApiServer::handleFactoryReset(AsyncWebServerRequest* req) {
    Config::factoryReset();
    req->send(200, "application/json", "{\"status\":\"factory_reset_done\",\"will_restart\":true}");
    delay(500);
    ESP.restart();
}

// ============================================================
//  WebSocket
// ============================================================

void ApiServer::onWsEvent(AsyncWebSocket* /*server*/, AsyncWebSocketClient* client,
                          AwsEventType type, void* /*arg*/, uint8_t* /*data*/, size_t /*len*/) {
    if (type == WS_EVT_CONNECT) {
        log_i("WS: client %u connected from %s",
              client->id(), client->remoteIP().toString().c_str());
    } else if (type == WS_EVT_DISCONNECT) {
        log_i("WS: client %u disconnected", client->id());
    }
}

void ApiServer::tick() {
    if (!_ws || _ws->count() == 0) return;

    uint32_t now = millis();
    if (now - _lastBroadcastMs < 1000) return;
    _lastBroadcastMs = now;

    auto& s = Stats::get();
    char buf[256];
    snprintf(buf, sizeof(buf),
        "{\"type\":\"tick\","
        "\"hashrate_hs\":%.1f,"
        "\"hashrate_1m_hs\":%.1f,"
        "\"shares_accepted\":%u,"
        "\"shares_rejected\":%u,"
        "\"best_diff\":%.6f,"
        "\"uptime_s\":%u}",
        s.hashrate1s.load(),
        s.hashrate1m.load(),
        s.sharesAccepted.load(),
        s.sharesRejected.load(),
        s.bestDifficulty.load(),
        Stats::uptimeSeconds()
    );
    _ws->textAll(buf);

    // Clean up dead clients
    _ws->cleanupClients();
}

} // namespace WroomMiner

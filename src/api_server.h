// ============================================================
//  api_server.h - REST API + WebSocket
//
//  Serves all /api/v1/* endpoints and /ws.
//  Optimized for HashHive integration.
// ============================================================
#pragma once

#include <ESPAsyncWebServer.h>
#include "config.h"

namespace WroomMiner {

class ApiServer {
public:
    ApiServer();

    // Starts the server on the port configured in cfg.apiPort.
    void start(MinerConfig* cfg);

    void stop();

    // Periodic tick - pushes hashrate updates to WebSocket clients.
    // Should be called about once per second.
    void tick();

private:
    void setupRoutes();

    // --- GET handlers ---
    void handleStatus    (AsyncWebServerRequest* req);
    void handleMining    (AsyncWebServerRequest* req);
    void handlePool      (AsyncWebServerRequest* req);
    void handleNetwork   (AsyncWebServerRequest* req);
    void handleSystem    (AsyncWebServerRequest* req);
    void handleConfigGet (AsyncWebServerRequest* req);
    void handleStats     (AsyncWebServerRequest* req);
    void handleInfo      (AsyncWebServerRequest* req);

    // --- POST handlers ---
    void handleConfigPost (AsyncWebServerRequest* req, JsonVariant& body);
    void handleRestart    (AsyncWebServerRequest* req);
    void handleFactoryReset(AsyncWebServerRequest* req);

    // --- WebSocket ---
    void onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                   AwsEventType type, void* arg, uint8_t* data, size_t len);

    AsyncWebServer*  _server = nullptr;
    AsyncWebSocket*  _ws     = nullptr;
    MinerConfig*     _cfg    = nullptr;
    uint32_t         _lastBroadcastMs = 0;
};

} // namespace WroomMiner

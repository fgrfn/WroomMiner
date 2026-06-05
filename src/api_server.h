// ============================================================
//  api_server.h - REST API + WebSocket
//
//  Serves all /api/* endpoints and /ws.
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

    void setPoolState(const String& activeKind,
                      const String& activeUrl,
                      uint16_t activePort,
                      bool connected);

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
    void handleProbe           (AsyncWebServerRequest* req);

    // --- POST handlers ---
    void handleConfigPost (AsyncWebServerRequest* req, JsonVariant& body);
    void handleOtaPost    (AsyncWebServerRequest* req);
    void handleOtaUpload  (AsyncWebServerRequest* req,
                           const String& filename,
                           size_t index,
                           uint8_t* data,
                           size_t len,
                           bool final);
    void handleRestart    (AsyncWebServerRequest* req);
    void handleFactoryReset(AsyncWebServerRequest* req);

    // --- WebSocket ---
    void onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                   AwsEventType type, void* arg, uint8_t* data, size_t len);

    AsyncWebServer*  _server = nullptr;
    AsyncWebSocket*  _ws     = nullptr;
    MinerConfig*     _cfg    = nullptr;
    uint32_t         _lastBroadcastMs = 0;
    String           _activePoolKind = "none";
    String           _activePoolUrl = "";
    uint16_t         _activePoolPort = 0;
    bool             _poolConnected = false;
    bool             _otaFailed = false;
    size_t           _otaBytes = 0;
};

} // namespace WroomMiner

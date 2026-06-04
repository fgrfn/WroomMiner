// ============================================================
//  discovery.cpp - UDP LAN discovery for HashHive
// ============================================================
#include "discovery.h"
#include "stats.h"
#include <WiFi.h>

namespace WroomMiner {

static constexpr uint16_t DISCOVERY_PORT = 12345;

void DiscoveryBroadcaster::start(MinerConfig* cfg) {
    _cfg = cfg;
    _started = _udp.begin(0);
    if (_started) {
        Serial.printf("[discovery] UDP broadcast enabled on port %u\r\n", DISCOVERY_PORT);
    } else {
        Serial.println("[discovery] UDP begin failed");
    }
}

void DiscoveryBroadcaster::tick() {
    if (!_cfg || !_started || _cfg->udpBroadcastSec == 0 || WiFi.status() != WL_CONNECTED) {
        return;
    }

    uint32_t now = millis();
    uint32_t intervalMs = uint32_t(_cfg->udpBroadcastSec) * 1000UL;
    if (now - _lastBroadcastMs < intervalMs) {
        return;
    }
    _lastBroadcastMs = now;

    auto& s = Stats::get();
    char payload[384];
    snprintf(payload, sizeof(payload),
             "{\"type\":\"wroomminer_discovery\","
             "\"name\":\"%s\","
             "\"version\":\"%s\","
             "\"compatible_with\":\"HashHive\","
             "\"ip\":\"%s\","
             "\"mac\":\"%s\","
             "\"api_port\":%u,"
             "\"api_base\":\"http://%s:%u\","
             "\"ws_path\":\"/ws\","
             "\"hashrate_hs\":%.1f,"
             "\"shares_accepted\":%lu,"
             "\"uptime_seconds\":%lu}",
             WROOMMINER_NAME,
             WROOMMINER_VERSION,
             WiFi.localIP().toString().c_str(),
             WiFi.macAddress().c_str(),
             _cfg->apiPort,
             WiFi.localIP().toString().c_str(),
             _cfg->apiPort,
             s.hashrate1m.load(),
             static_cast<unsigned long>(s.sharesAccepted.load()),
             static_cast<unsigned long>(Stats::uptimeSeconds()));

    _udp.beginPacket(IPAddress(255, 255, 255, 255), DISCOVERY_PORT);
    _udp.write(reinterpret_cast<const uint8_t*>(payload), strlen(payload));
    _udp.endPacket();
}

} // namespace WroomMiner

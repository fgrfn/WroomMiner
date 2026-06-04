// ============================================================
//  discovery.h - UDP LAN discovery for HashHive
// ============================================================
#pragma once

#include <Arduino.h>
#include <WiFiUdp.h>
#include "config.h"

namespace WroomMiner {

class DiscoveryBroadcaster {
public:
    void start(MinerConfig* cfg);
    void tick();

private:
    MinerConfig* _cfg = nullptr;
    WiFiUDP      _udp;
    uint32_t     _lastBroadcastMs = 0;
    bool         _started = false;
};

} // namespace WroomMiner

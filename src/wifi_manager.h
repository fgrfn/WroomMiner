// ============================================================
//  wifi_manager.h - WiFi + captive portal setup
// ============================================================
#pragma once

#include <Arduino.h>
#include "config.h"

namespace WroomMiner {

class WifiSetup {
public:
    // First tries to connect with stored credentials.
    // If that fails: starts AP "WroomMiner-Setup" with a captive
    // portal. Blocks until configuration is complete.
    static bool connectOrConfig(MinerConfig& cfg);

    // Forces AP mode (e.g. when the boot button is held).
    static void startConfigPortal(MinerConfig& cfg);

    static bool isConnected();
    static String localIp();
    static int rssi();
};

} // namespace WroomMiner

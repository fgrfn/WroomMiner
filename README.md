# WroomMiner

A lean, **fully open-source** Bitcoin solo-mining firmware for ESP32-WROOM-32D modules, featuring a **complete REST API + WebSocket** for seamless integration with [HashHive](https://github.com/fgrfn/hashhive).

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](https://opensource.org/licenses/MIT)
[![PlatformIO](https://img.shields.io/badge/PlatformIO-ESP32-orange.svg)](https://platformio.org/)

---

## What is WroomMiner?

WroomMiner is a solo-mining firmware for ESP32-WROOM-32D boards that combines the best of [NerdMiner v2](https://github.com/BitMaker-hub/NerdMiner_v2) (open source, proven Stratum stack) and [NMminer](https://github.com/NMminer1024/NMMiner) (performance optimizations, pool fallback), extended with a **complete REST API**.

This turns the miner into a **first-class citizen** in HashHive — comparable to BitAxe and NerdAxe, but for ESP32 hardware.

### What sets it apart from NerdMiner / NMminer

- **Complete REST API** with 10+ endpoints (`/api/...`)
- **WebSocket livestream** for real-time hashrate and share events
- **UDP LAN broadcast** for automatic discovery by HashHive
- **OTA updates** via the API — no more USB cable required
- **Primary + fallback pool** with automatic switching
- **NVS-based persistence** for session stats and best difficulty
- **No display required** — lean and headless-optimized

---

## Hardware Requirements

- ESP32-WROOM-32D module (ESP32-D0WDQ6-V3 chip)
- 4 MB flash (standard on WROOM-32D)
- WiFi 2.4 GHz
- USB connection for initial flashing (OTA afterwards)

Performance target: **400–600 KH/s** in Phase 1, with tuning from NMminer towards **800+ KH/s**.

---

## Quick Start

```bash
# Clone the repository
git clone https://github.com/fgrfn/WroomMiner.git
cd WroomMiner

# Build and flash with PlatformIO
pio run -t upload

# Open the serial monitor
pio device monitor
```

### Initial Setup

1. After flashing, WroomMiner boots into **AP mode** with the SSID `WroomMiner-Setup`
2. Connect (no password) and open `http://192.168.4.1`
3. Enter WiFi, pool URL and BTC wallet
4. Save → the miner starts automatically
5. Query status via `http://<miner-ip>/api/status`

### Flashing in the browser

WroomMiner can also be flashed without PlatformIO using a Web Serial flasher
(Chrome/Edge only). See [`docs/FLASHING.md`](docs/FLASHING.md) for the binary
offsets and tooling options (esptool-js, Adafruit WebSerial ESPTool).

---

## REST API

Full documentation in [`docs/API.md`](docs/API.md). Overview:

| Endpoint | Method | Description |
|---|---|---|
| `/api/probe` | GET | Compact discovery snapshot |
| `/api/status` | GET | Compact system overview |
| `/api/mining` | GET | Hashrate, shares, best diff, uptime |
| `/api/pool` | GET | Pool URL, port, connection status |
| `/api/network` | GET | SSID, RSSI, IP, MAC |
| `/api/system` | GET | Chip info, heap, CPU frequency, temperature |
| `/api/config` | GET/POST | Read/write configuration |
| `/api/stats` | GET | Session + all-time statistics |
| `/api/info` | GET | Firmware version, build date |
| `/api/system/restart` | POST | Soft reboot |
| `/api/system/reset` | POST | Factory reset (clears NVS) |
| `/api/ota` | POST | OTA firmware update |
| `/ws` | WS | WebSocket livestream |

---

## Project Structure

```
WroomMiner/
├── platformio.ini      # PlatformIO build config
├── partitions.csv      # 2x OTA slots + NVS + 192 KB SPIFFS
├── src/
│   ├── main.cpp        # Setup, task spawning
│   ├── config.{h,cpp}  # NVS-based configuration
│   ├── wifi_manager.*  # WiFi + AP config mode
│   ├── stratum.*       # Stratum v1 client (TCP + JSON)
│   ├── mining.*        # Mining task (Core 1)
│   ├── sha256d.*       # SHA256d (HW-accelerated)
│   ├── api_server.*    # REST API + WebSocket (Core 0)
│   └── stats.*         # Hashrate tracking, persistence
└── docs/
    ├── API.md          # API reference
    └── FLASHING.md     # Flashing guide
```

---

## Roadmap

### Phase 1 — Core (in progress)
- [x] Project scaffold
- [ ] SHA256d works + verified
- [ ] Stratum v1 subscribe/authorize/submit
- [ ] First share accepted
- [ ] REST API serves real data

### Phase 2 — Features
- [ ] WiFiManager AP config mode
- [ ] NVS persistence for session stats
- [ ] WebSocket livestream
- [ ] UDP LAN broadcast (HashHive discovery)
- [ ] Primary + fallback pool

### Phase 3 — Optimization & Polish
- [ ] Hardware SHA256 acceleration
- [ ] Midstate optimization
- [ ] OTA updates via API
- [ ] HashHive integration test
- [ ] Web UI on SPIFFS

---

## Development

This project is designed to be built and iterated on with
[Claude Code](https://docs.claude.com/en/docs/claude-code) or any standard
PlatformIO workflow. The `docs/` folder describes the architecture so an agent
can pick up where the scaffold leaves off.

The most important next implementation step is the **Merkle root calculation**
in `mining.cpp::buildHeader()` — see the TODO there. Without it, shares will be
computed but rejected by the pool.

---

## License

MIT — see [LICENSE](LICENSE).

## Credits

- [NerdMiner v2](https://github.com/BitMaker-hub/NerdMiner_v2) (BitMaker-hub) — base concept, Stratum implementation
- [NMminer](https://github.com/NMminer1024/NMMiner) (NMTech) — performance inspiration
- [Public Pool](https://web.public-pool.io) — test pool with low share difficulty

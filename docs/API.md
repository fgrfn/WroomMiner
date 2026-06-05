# WroomMiner REST API

Complete reference for all HTTP endpoints. Base URL: `http://<miner-ip>` (default port 80).

All responses are JSON. Hashrates are in **H/s** (hashes per second), times in **seconds** unless noted otherwise. All field names are `snake_case`.

---

## Overview

| Endpoint | Method | Description |
|---|---|---|
| `/api/probe` | GET | Compact discovery snapshot |
| `/api/status` | GET | System overview for dashboard cards |
| `/api/mining` | GET | Detailed mining statistics |
| `/api/pool` | GET | Pool configuration & connection state |
| `/api/network` | GET | WiFi details |
| `/api/system` | GET | Hardware info |
| `/api/config` | GET | Current configuration (no secrets) |
| `/api/config` | POST | Change configuration |
| `/api/stats` | GET | Extended statistics |
| `/api/info` | GET | Firmware info |
| `/api/system/restart` | POST | Soft reboot |
| `/api/system/reset` | POST | Factory reset (clears NVS) |
| `/api/ota` | POST | OTA firmware update |
| `/ws` | WS | WebSocket livestream |

Legacy `/api/v1/*` paths redirect (301) to the canonical paths above.

---

## GET `/api/probe`

Compact snapshot for LAN discovery and health checks.

```json
{
  "firmware": "WroomMiner",
  "version": "0.1.0",
  "hostname": "wroomminer",
  "mac": "AA:BB:CC:DD:EE:FF",
  "ip": "192.168.1.123",
  "uptime_seconds": 3712,
  "hashrate_hs": 412000.5,
  "hashrate_1m_hs": 411500.0,
  "shares_accepted": 8,
  "best_difficulty": 0.12345
}
```

---

## GET `/api/status`

Compact overview for dashboard cards.

```json
{
  "uptime_seconds": 3712,
  "hashrate_hs": 412000.5,
  "shares_accepted": 8,
  "shares_rejected": 0,
  "best_difficulty": 0.12345,
  "wifi_connected": true,
  "wifi_rssi": -54,
  "free_heap": 142336,
  "firmware_version": "0.1.0"
}
```

---

## GET `/api/mining`

Detailed mining statistics.

```json
{
  "hashrate_1s_hs": 415000,
  "hashrate_1m_hs": 412000,
  "total_hashes": 1532000000,
  "shares_accepted": 8,
  "shares_rejected": 0,
  "shares_submitted": 8,
  "blocks_found": 0,
  "best_difficulty": 0.12345,
  "session_best_diff": 0.05432,
  "uptime_seconds": 3712,
  "last_share_ago_ms": 142000,
  "last_share_ago_seconds": 142
}
```

---

## GET `/api/pool`

Pool configuration and active connection state.

```json
{
  "primary": {
    "url": "public-pool.io",
    "port": 21496
  },
  "fallback": {
    "url": "solo.ckpool.org",
    "port": 3333
  },
  "active": "fallback",
  "active_url": "solo.ckpool.org",
  "active_port": 3333,
  "connected": true,
  "worker": "bc1q....wroom01",
  "active_wallet": "bc1q..."
}
```

---

## GET `/api/network`

WiFi connection details.

```json
{
  "ssid": "MyWifi",
  "rssi": -54,
  "ip": "192.168.1.123",
  "mac": "AA:BB:CC:DD:EE:FF",
  "gateway": "192.168.1.1",
  "dns": "192.168.1.1",
  "channel": 6,
  "hostname": "wroomminer"
}
```

---

## GET `/api/system`

Hardware and runtime info.

```json
{
  "chip_model": "ESP32",
  "chip_cores": 2,
  "chip_revision": 3,
  "cpu_freq_mhz": 240,
  "flash_size": 4194304,
  "sketch_size": 980000,
  "sketch_free": 1032192,
  "free_heap": 142336,
  "min_free_heap": 138000,
  "heap_size": 286000,
  "idf_version": "v4.4.6",
  "temperature_c": -1,
  "uptime_seconds": 3712
}
```

**Note:** The ESP32-WROOM-32D has no internal temperature sensor — `temperature_c` is always `-1`.

---

## GET `/api/config`

Current configuration. Pool passwords are **not** returned.

```json
{
  "pool_primary_url": "public-pool.io",
  "pool_primary_port": 21496,
  "pool_primary_suggest_difficulty": 0.00015,
  "pool_primary_min_submit_difficulty": 0.001,
  "pool_fallback_url": "solo.ckpool.org",
  "pool_fallback_port": 3333,
  "pool_fallback_suggest_difficulty": 0.00015,
  "pool_fallback_min_submit_difficulty": 0.001,
  "wallet_address": "bc1q...",
  "wallet_fallback_address": "bc1q...",
  "worker_name": "wroom01",
  "wifi_ssid": "MyWifi",
  "led_enabled": true,
  "udp_broadcast_sec": 5,
  "api_port": 80
}
```

---

## POST `/api/config`

Change configuration. Only the provided fields are updated. A restart is required after saving.

**Request:**
```json
{
  "pool_primary_url": "pool.nerdminers.org",
  "pool_primary_port": 3333,
  "pool_primary_suggest_difficulty": 0.00015,
  "pool_primary_min_submit_difficulty": 0.001,
  "pool_primary_password": "x",
  "pool_fallback_password": "x",
  "wallet_fallback_address": "bc1q...",
  "worker_name": "wroom02"
}
```

**Response:**
```json
{
  "status": "saved",
  "restart_required": true
}
```

---

## POST `/api/system/restart`

Soft reboot of the miner.

```json
{ "status": "restarting" }
```

---

## POST `/api/system/reset`

Factory reset — clears all NVS entries and reboots. WiFi setup is required again afterwards.

```json
{ "status": "factory_reset_done", "will_restart": true }
```

---

## POST `/api/ota`

Uploads a compiled PlatformIO firmware image (`firmware.bin`) as multipart form data. On success the miner restarts automatically.

**Request:**
```bash
curl -X POST http://192.168.1.123/api/ota \
  -F firmware=@.pio/build/esp32dev/firmware.bin
```

**Response:**
```json
{
  "status": "ok",
  "bytes": 987000,
  "will_restart": true
}
```

---

## GET `/api/info`

Firmware metadata.

```json
{
  "firmware": "WroomMiner",
  "version": "0.1.0",
  "build_date": "Jun  5 2026 12:00:00",
  "api_version": "1"
}
```

---

## WebSocket `/ws`

Sends a tick event once per second with current mining metrics.

```json
{
  "type": "tick",
  "hashrate_hs": 412000,
  "hashrate_1m_hs": 411500,
  "shares_accepted": 8,
  "shares_rejected": 0,
  "best_diff": 0.12345,
  "uptime_s": 3712
}
```

---

## UDP Discovery

WroomMiner broadcasts a JSON packet on UDP port `12345` every `udp_broadcast_sec` seconds when WiFi is connected. HashHive uses this for automatic device discovery.

```json
{
  "type": "wroomminer_discovery",
  "name": "WroomMiner",
  "version": "0.1.0",
  "ip": "192.168.1.123",
  "mac": "AA:BB:CC:DD:EE:FF",
  "api_port": 80,
  "hashrate_hs": 412000,
  "shares_accepted": 8,
  "uptime_seconds": 3712
}
```

---

## Example: cURL

```bash
# Discovery snapshot
curl http://192.168.1.123/api/probe

# Mining stats
curl http://192.168.1.123/api/mining

# Switch pool
curl -X POST http://192.168.1.123/api/config \
  -H "Content-Type: application/json" \
  -d '{"pool_primary_url":"pool.nerdminers.org","pool_primary_port":3333}'

# Restart
curl -X POST http://192.168.1.123/api/system/restart
```

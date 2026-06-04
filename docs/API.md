# WroomMiner REST API

Complete reference for all HTTP endpoints. Base URL: `http://<miner-ip>` (default port 80).

All responses are JSON. Hashrates are in **H/s** (hashes per second), times in **milliseconds** unless noted otherwise.

---

## Overview

| Endpoint | Method | Auth | Description |
|---|---|---|---|
| `/api/v1/status` | GET | — | Compact system overview |
| `/api/v1/mining` | GET | — | Detailed mining stats |
| `/api/v1/pool` | GET | — | Pool configuration & status |
| `/api/v1/network` | GET | — | WiFi details |
| `/api/v1/system` | GET | — | Hardware info |
| `/api/v1/config` | GET | — | Current configuration (no secrets) |
| `/api/v1/config` | POST | — | Change configuration |
| `/api/v1/stats` | GET | — | Extended statistics |
| `/api/v1/info` | GET | — | Firmware info |
| `/api/v1/action/restart` | POST | — | Reboot |
| `/api/v1/action/reset` | POST | — | Factory reset |
| `/ws` | WS | — | WebSocket livestream |

---

## GET `/api/v1/status`

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

## GET `/api/v1/mining`

Detailed mining statistics.

```json
{
  "hashrate_1s_hs": 415000,
  "hashrate_1m_hs": 412000,
  "total_hashes": 1532000000,
  "shares_accepted": 8,
  "shares_rejected": 0,
  "blocks_found": 0,
  "best_difficulty": 0.12345,
  "session_best_diff": 0.05432,
  "uptime_seconds": 3712,
  "last_share_ago_ms": 142000
}
```

---

## GET `/api/v1/pool`

Pool configuration and active status.

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
  "active": "primary",
  "connected": true,
  "worker": "wroom01"
}
```

---

## GET `/api/v1/network`

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

## GET `/api/v1/system`

Hardware info.

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

## GET `/api/v1/config`

Current configuration (the pool password is **not** returned).

```json
{
  "pool_primary_url": "public-pool.io",
  "pool_primary_port": 21496,
  "pool_fallback_url": "solo.ckpool.org",
  "pool_fallback_port": 3333,
  "wallet_address": "bc1q...",
  "worker_name": "wroom01",
  "wifi_ssid": "MyWifi",
  "led_enabled": true,
  "udp_broadcast_sec": 5,
  "api_port": 80
}
```

---

## POST `/api/v1/config`

Change configuration. Only the provided fields are updated. A restart is usually required after saving.

**Request:**
```json
{
  "pool_primary_url": "pool.nerdminers.org",
  "pool_primary_port": 3333,
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

## POST `/api/v1/action/restart`

Soft reboot of the miner.

```json
{ "status": "restarting" }
```

## POST `/api/v1/action/reset`

Factory reset — clears all NVS entries and reboots. WiFi setup is required again afterwards.

```json
{ "status": "factory_reset_done", "will_restart": true }
```

---

## WebSocket `/ws`

Sends a tick event once per second with current mining metrics — ideal for HashHive live dashboards.

**Event format:**
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

## Integration with HashHive

HashHive can register WroomMiner as a device type alongside BitAxe and NerdAxe. Recommended workflow:

1. **Discovery:** UDP broadcast on port `12345` (coming in Phase 2)
2. **Health check:** GET `/api/v1/info` → verify `compatible_with == "HashHive"`
3. **Live data:** WebSocket `/ws` for real-time hashrate
4. **Polling fallback:** GET `/api/v1/status` every 5–10 seconds

---

## Example: cURL

```bash
# Query status
curl http://192.168.1.123/api/v1/status

# Switch pool
curl -X POST http://192.168.1.123/api/v1/config \
  -H "Content-Type: application/json" \
  -d '{"pool_primary_url":"pool.nerdminers.org","pool_primary_port":3333}'

# Restart
curl -X POST http://192.168.1.123/api/v1/action/restart
```

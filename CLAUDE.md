ok # CLAUDE.md

Context for working on WroomMiner with Claude Code.

## What this is

WroomMiner is an open-source Bitcoin solo-mining firmware for ESP32-WROOM-32D
boards (4 MB flash). It is built with PlatformIO + the Arduino framework. The
defining feature versus NerdMiner v2 and NMminer is a complete REST API +
WebSocket, designed for integration with the HashHive dashboard.

## Architecture

- **Core 1 (APP_CPU):** mining task — SHA256d loop, nonce iteration, share
  detection. See `src/mining.cpp`.
- **Core 0 (PRO_CPU):** everything else — WiFi, Stratum TCP client, async HTTP
  server, WebSocket. Driven from `loop()` in `src/main.cpp`.
- **Cross-core state:** `src/stats.*` uses `std::atomic` so the API server can
  read counters the mining task writes, without locks.
- **Persistence:** `src/config.*` and the persist/restore methods in
  `src/stats.*` use NVS via the Preferences library.

## Build & test

```bash
pio run                 # build for esp32dev (ESP32-WROOM-32D)
pio run -t upload       # flash via USB
pio device monitor      # serial log at 115200
```

Always run `pio run` after changes and fix any compiler errors before
considering a task done.

## Highest-priority TODO

`src/mining.cpp::buildHeader()` currently zeroes the Merkle root
(`memset(header80 + 36, 0, 32)`). Until this is implemented, shares are computed
but the pool will reject them. The correct algorithm (Stratum v1):

1. `coinbase = coinbase1 + extranonce1 + extranonce2 + coinbase2` (hex → bytes)
2. `merkleRoot = sha256d(coinbase)`
3. for each branch in `job.merkleBranches[0..merkleCount]`:
   `merkleRoot = sha256d(merkleRoot || branch)`
4. write `merkleRoot` into `header80[36..67]` (watch endianness)

After that, validate the full target check against the pool's share difficulty
(the current code uses a placeholder `hashHi == 0` test).

## Other open items

- Real midstate optimization in `src/sha256d.cpp::sha256d_with_midstate()`
  (currently a naive fallback).
- Hardware SHA256 via `esp_sha` (Phase 3).
- UDP LAN broadcast for HashHive discovery (Phase 2).
- `POST /api/v1/ota` endpoint (Phase 3).
- Dynamic `active` pool reporting in `api_server.cpp::handlePool()`.

## Conventions

- All code, comments, and docs in **English**.
- Namespace everything under `WroomMiner`.
- Keep the mining loop allocation-free and watchdog-safe (`vTaskDelay(1)` per
  batch).
- JSON field names in the API are `snake_case` and must stay stable — HashHive
  depends on them. Document any change in `docs/API.md`.

# Flashing WroomMiner

There are three ways to get the firmware onto an ESP32-WROOM-32D.

---

## Option A — PlatformIO (recommended for development)

```bash
pio run -t upload
pio device monitor
```

This handles the bootloader, partition table, and app binary automatically.

---

## Option B — Browser flasher (recommended for end users)

Web Serial flashers run entirely in the browser. **Chrome or Edge only** —
Firefox and Safari do not implement the Web Serial API.

### B.1 — esptool-js (preferred)

Espressif's official tool, kept current and aware of all ESP32 chips:

<https://espressif.github.io/esptool-js/>

1. Connect the board via USB, click **Connect**, select the serial port.
2. Add the four binaries with the offsets from the table below.
3. Click **Program**.

### B.2 — Adafruit WebSerial ESPTool

<https://github.com/adafruit/Adafruit_WebSerial_ESPTool>
(hosted version: <https://adafruit.github.io/Adafruit_WebSerial_ESPTool/>)

Works the same way, but is older and tuned for Adafruit boards. For a generic
WROOM-32D it is fine as long as you provide the correct offsets. esptool-js is
the better-maintained choice.

### Binary offsets (ESP32, 4 MB)

| File | Offset |
|---|---|
| `bootloader.bin` | `0x1000` |
| `partitions.bin` | `0x8000` |
| `boot_app0.bin` | `0xe000` |
| `firmware.bin` | `0x10000` |

After a PlatformIO build, these files are located under:

```
.pio/build/esp32dev/bootloader.bin
.pio/build/esp32dev/partitions.bin
.pio/build/esp32dev/firmware.bin
```

`boot_app0.bin` ships with the Arduino-ESP32 core, typically at:

```
~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin
```

A combined single-file binary can also be produced (see the merge step below),
which avoids juggling four files in the browser.

### Producing a merged binary

```bash
esptool.py --chip esp32 merge_bin -o WroomMiner-merged.bin \
  --flash_mode dio --flash_size 4MB \
  0x1000  .pio/build/esp32dev/bootloader.bin \
  0x8000  .pio/build/esp32dev/partitions.bin \
  0xe000  boot_app0.bin \
  0x10000 .pio/build/esp32dev/firmware.bin
```

Then flash `WroomMiner-merged.bin` at offset `0x0`.

---

## Option C — OTA (after the first flash)

Once a miner is running and reachable, no cable is needed:

```bash
pio run -e esp32dev-ota -t upload   # set upload_port to the miner IP
```

Or, in Phase 3, via `POST /api/v1/ota` — which is also how HashHive will push
updates to the whole fleet.

---

## Troubleshooting

- **Upload stalls / fails:** put the board into bootloader mode — hold the
  `BOOT` button, tap `EN`/`RST`, release `BOOT`, then start the upload.
- **WiFi fails after flashing:** do a full **Erase all flash** in the browser
  tool, then reflash. Stale NVS data is a common cause.
- **Wrong port / no device:** check the USB cable (some are charge-only) and the
  CH340/CP210x driver on your OS.

# XUNA firmware — local MVP

Minimal offline hardware bring-up for the **standard LILYGO T-Display-S3 LCD** (170×320 ST7789, ESP32-S3R8, expected 16 MB Flash / 8 MB OPI PSRAM). Firmware has been uploaded and basic LCD/button checks passed on 2026-10-11 by owner observation; full M01 validation and exact PCB revision remain pending.

## Setup and commands

Install VS Code + PlatformIO IDE. Open this `firmware/` folder and use Build, Upload and Monitor, or run these commands from the parent repository in a PlatformIO terminal:

```sh
pio run -d firmware -e xuna_t_display_s3
pio device list
pio run -d firmware -e xuna_t_display_s3 -t upload --upload-port /dev/cu.usbmodemYOUR_PORT
pio device monitor -b 115200 --port /dev/cu.usbmodemYOUR_PORT
```

Replace the port with the detected device. M01 was uploaded on 2026-10-11 after owner confirmation. Upload overwrites the board's current firmware. If upload fails, hold BOOT (GPIO0), tap RST, release BOOT, upload, then tap RST. Try `--upload-speed 460800` if needed. USB-C must support data. Reopen Monitor after the port re-enumerates; reset with Monitor open to capture startup logs.

## Configuration and official sources

- `espressif32@6.5.0` / Arduino ESP32 2.0.14, matching the vendor's baseline.
- PlatformIO's built-in `esp32-s3-devkitc-1` provides the MCU target only. Explicit overrides match the vendor board manifest: 240 MHz CPU, 80 MHz QIO Flash, `qio_opi` memory, 16 MB Flash and `default_16MB.csv` partitions. The generic board name does **not** define the LCD pins.
- `bodmer/TFT_eSPI@2.5.43`, with its bundled **Setup206** force-included for all translation units and `USER_SETUP_LOADED`. No dependency files need manual editing.
- Native USB CDC/JTAG enabled; Serial 115200. Application does not wait for a serial host. Standalone boot still needs physical verification.
- LCD uses **8-bit parallel**, not SPI: D0–D7 = 39,40,41,42,45,46,47,48; CS=6, DC=7, RST=5, WR=8, RD=9. GPIO15 enables power; GPIO38 controls backlight. Buttons GPIO0/GPIO14 are active-low with pull-ups. GPIO0 held during reset enters download mode.
- Rotation defaults to `1` (320×170); change `kRotation` in `include/AppConfig.h` to 0–3 and rebuild. Layout adapts to panel dimensions.

Primary sources reviewed on 2026-10-10:

- [LILYGO README / pinout / FAQ](https://github.com/Xinyuan-LilyGO/T-Display-S3)
- [Vendor PlatformIO baseline](https://github.com/Xinyuan-LilyGO/T-Display-S3/blob/main/platformio.ini)
- [Vendor board manifest](https://github.com/Xinyuan-LilyGO/T-Display-S3/blob/main/boards/lilygo-t-display-s3.json)
- [Vendor Arduino_GFX parallel LCD example](https://github.com/Xinyuan-LilyGO/T-Display-S3/blob/main/examples/Arduino_GFXDemo/Arduino_GFXDemo.ino)
- [TFT_eSPI Setup206](https://github.com/Bodmer/TFT_eSPI/blob/master/User_Setups/Setup206_LilyGo_T_Display_S3.h)

TFT_eSPI was chosen for its maintained board setup and small sprite support. Arduino_GFX is the vendor's alternative and a useful diagnostic if the display stays blank. Newer Arduino cores are outside this pinned bring-up baseline. Build warnings from upstream include unavailable DMA in parallel mode, unconfigured touch (expected for standard LCD), and a non-void return warning in Arduino 2.0.14 `esp32-hal-uart.c`. Dependencies are kept unchanged.

## Small modular architecture

- `main.cpp`: initialization, input-to-animation mapping, bounded loop and diagnostics.
- `DisplayManager`: LCD setup, branding, original pixel sprite and local redraws.
- `InputManager`: GPIO adapter; `DebouncedButton`: pure C++ edge debounce.
- `AnimationManager`: pure C++ state/frame timing with explicit timestamps.
- `AppConfig`: version, board controls and timing constants.

BOOT requests happy; GPIO14 requests idle. Simultaneous presses resolve to idle. Happy lasts 2400 ms; another BOOT press restarts it. Both states have two frames (idle 400 ms, happy 160 ms). Held buttons do not auto-repeat, and a button held at application startup must be released before a new press is emitted. Unsigned elapsed-time comparisons handle timer rollover. Late frames are skipped without catch-up loops. These are scripted reactions, not learning.

The display allocates one 80×80 RGB565 sprite (12,800 bytes plus library overhead) during setup. There is no per-frame application allocation or full-screen repaint; only the sprite region is transferred. Setup/driver initialization may block briefly; the application loop has no delays or serial-host wait. Render and serial writes are synchronous, so actual input latency needs measurement.

Serial logs startup version, Flash size, PSRAM detection, chip capacity via the pinned ESP-IDF `esp_spiram_get_size()` API, and display allocation status. `ESP.getPsramSize()` describes the PSRAM heap, so it is logged as `psram_heap_total` and is not compared to physical chip capacity. Every 5 seconds they report free/min heap, total/free PSRAM heap, state, actual rendered frames per second, average/max render microseconds and maximum loop gap. FPS is the animation update rate, not panel refresh capacity. Flash/PSRAM mismatches produce a warning. Allocation failure disables display and leaves input/logging running. TFT_eSPI cannot detect an absent or faulty physical LCD; successful initialization must be checked visually.

## Host tests

No hardware or PlatformIO dependencies are needed:

```sh
c++ -std=c++11 -Wall -Wextra -Werror -Ifirmware/include firmware/test/test_core.cpp -o /tmp/xuna-m01-core-test
/tmp/xuna-m01-core-test
```

Tests cover bounce, hold/release, held-at-boot, timer rollover, frame boundaries, happy expiry/retrigger, explicit idle and bounded delayed updates. They do not validate electrical input, USB, LCD or memory hardware.

## Licensing

The pet sprite is original project artwork. No commercial pet assets or vendor application code are copied. Platform/framework packages retain upstream licenses; Arduino-ESP32 is LGPL-2.1 and ESP-IDF components have their own notices. TFT_eSPI includes MIT/BSD-derived code and font-specific notices: see its packaged `license.txt` and `Fonts` notices. Preserve applicable upstream notices when redistributing dependencies or firmware; the repository has not selected a license for original XUNA code.

See [M01 validation checklist](../docs/roadmap/M01-bring-up.md) in the parent repository for outstanding hardware checks.

## Local MVP through M05 — 2026-10-11

Current source version 0.2.0-mvp retains pinned display/toolchain and existing PSRAM capacity fix. Adds idle/happy/sleep/curious sprite reactions, bundled Arduino BLE adapter, pure C++ LifeEngine/LocalBehavior/BleProtocol and Preferences-backed two-slot snapshot storage. Local life/preference survives via bounded versioned snapshots; transient animation boots idle. Feedback is explicit, context-specific and associated with one suggestion; eight source-tagged feedback episodes retained. Quiet-time routine uses learned preference after 60 seconds without direct activity, while sleep remains explicit. No wall-clock/offline elapsed time is fabricated.

BLE callbacks enqueue at most eight packets; one processed per loop. Main loop alone mutates animation/life, with local input priority. Attribute values only refresh when changed. Persistence writes on confirmed feedback, explicit forget and five-minute active-time checkpoint; not per frame. Snapshot checksum is an integrity check, not cryptographic authentication. NVS atomicity/power-loss and BLE resource/stability behavior require actual device testing. First boot checks key existence before reading empty NVS slots.

Additional host checks:

```sh
c++ -std=c++11 -Wall -Wextra -Werror -Ifirmware/include firmware/test/test_life.cpp -o /tmp/xuna-life-test
/tmp/xuna-life-test
```

BLE/Preferences are bundled with Arduino2.0.14; no new firmware dependency/version upgrade. Retain framework and bundled BLE license notices. Parent docs/roadmap/MVP-local.md owns exact protocol, software evidence and pending physical acceptance. Historic M01 observations above do not validate changed MVP behavior.

MVP build/upload and corrected first-boot startup PASS on LILYGO2026-10-11; owner confirmed LCD and both buttons. Final linker RAM47504 bytes, firmware964333 bytes. Short diagnostics are recorded in parent MVP record; physical BLE/learning/NVS recovery and long-run validation remain pending.

# XUNA firmware — animated pixel pet scene

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
- Rotation defaults to `1` (320×170); change `kRotation` in `src/BoardConfig.h` to 0–3 and rebuild. Layout adapts to panel dimensions.

Primary sources reviewed on 2026-10-10:

- [LILYGO README / pinout / FAQ](https://github.com/Xinyuan-LilyGO/T-Display-S3)
- [Vendor PlatformIO baseline](https://github.com/Xinyuan-LilyGO/T-Display-S3/blob/main/platformio.ini)
- [Vendor board manifest](https://github.com/Xinyuan-LilyGO/T-Display-S3/blob/main/boards/lilygo-t-display-s3.json)
- [Vendor Arduino_GFX parallel LCD example](https://github.com/Xinyuan-LilyGO/T-Display-S3/blob/main/examples/Arduino_GFXDemo/Arduino_GFXDemo.ino)
- [TFT_eSPI Setup206](https://github.com/Bodmer/TFT_eSPI/blob/master/User_Setups/Setup206_LilyGo_T_Display_S3.h)

TFT_eSPI was chosen for its maintained board setup and small sprite support. Arduino_GFX is the vendor's alternative and a useful diagnostic if the display stays blank. Newer Arduino cores are outside this pinned bring-up baseline. Build warnings from upstream include unavailable DMA in parallel mode, unconfigured touch (expected for standard LCD), and a non-void return warning in Arduino 2.0.14 `esp32-hal-uart.c`. Dependencies are kept unchanged.

## Historical MVP animation architecture (before static scene update)

- `main.cpp`: initialization, input-to-animation mapping, bounded loop and diagnostics.
- `DisplayManager`: LCD setup, branding, original pixel sprite and local redraws.
- `InputManager`: GPIO adapter; `DebouncedButton`: pure C++ edge debounce.
- `AnimationManager`: pure C++ state/frame timing with explicit timestamps.
- `src/BoardConfig.h`: version and board controls; `xuna/CoreConfig.h`: pure timing constants.

BOOT requests happy; GPIO14 requests idle. Simultaneous presses resolve to idle. Happy lasts 2400 ms; another BOOT press restarts it. Both states have two frames (idle 400 ms, happy 160 ms). Held buttons do not auto-repeat, and a button held at application startup must be released before a new press is emitted. Unsigned elapsed-time comparisons handle timer rollover. Late frames are skipped without catch-up loops. These are scripted reactions, not learning.

The display allocates one 80×80 RGB565 sprite (12,800 bytes plus library overhead) during setup. There is no per-frame application allocation or full-screen repaint; only the sprite region is transferred. Setup/driver initialization may block briefly; the application loop has no delays or serial-host wait. Render and serial writes are synchronous, so actual input latency needs measurement.

Serial logs startup version, Flash size, PSRAM detection, chip capacity via the pinned ESP-IDF `esp_spiram_get_size()` API, and display allocation status. `ESP.getPsramSize()` describes the PSRAM heap, so it is logged as `psram_heap_total` and is not compared to physical chip capacity. Every 5 seconds they report free/min heap, total/free PSRAM heap, state, actual rendered frames per second, average/max render microseconds and maximum loop gap. FPS is the animation update rate, not panel refresh capacity. Flash/PSRAM mismatches produce a warning. Allocation failure disables display and leaves input/logging running. TFT_eSPI cannot detect an absent or faulty physical LCD; successful initialization must be checked visually.

## Host tests and portable core

Pure logic lives under `lib/xuna_core/include/xuna/` (C++11, no hardware dependencies). PlatformIO native tests compile the same headers as ESP32; Arduino adapter sources are excluded by the default `test_build_src = no`.

```sh
pio test -d firmware -e native
pio test -d firmware -e native_asan  # macOS/Linux/WSL with sanitizer-capable compiler
pio run -d firmware -e xuna_t_display_s3
```

Three Unity suites retain the original core/life assertions and S03 characterization fixtures. `test/fixtures/v1_snapshots.h` is a frozen host-generated v1 baseline; never regenerate it just to accommodate a regression. Tests cover input/animation timing, preference learning, snapshot bytes/recovery and command/cache semantics. They do not validate physical LCD, electrical input, USB, BLE or flash power-loss behavior.

`tools/check_core_purity.py` runs before every environment build and rejects hardware includes inside the portable core. Library metadata declares no Arduino or framework dependency. Keep board pins and driver configuration in adapters. On this Mac, PlatformIO is callable at `~/.platformio/penv/bin/pio` when it is absent from shell PATH.

## Licensing

The pet sprite is original project artwork. No commercial pet assets or vendor application code are copied. Platform/framework packages retain upstream licenses; Arduino-ESP32 is LGPL-2.1 and ESP-IDF components have their own notices. TFT_eSPI includes MIT/BSD-derived code and font-specific notices: see its packaged `license.txt` and `Fonts` notices. Device cards (2026-10-22) add `lib/qrcodegen` (Project Nayuki QR Code generator v1.8.0, MIT, unchanged, `lib/qrcodegen/LICENSE`) and glyph bitmaps rendered from Be Vietnam Pro (OFL-1.1, `assets/licenses/OFL-BeVietnamPro.txt`; generator `tools/generate_ui_font.py`). Preserve applicable upstream notices when redistributing dependencies or firmware; the repository has not selected a license for original XUNA code.

See [M01 validation checklist](../docs/roadmap/M01-bring-up.md) in the parent repository for outstanding hardware checks.

## Local MVP through M05 — 2026-10-11

Current source version 0.2.0-mvp retains pinned display/toolchain and existing PSRAM capacity fix. Adds idle/happy/sleep/curious sprite reactions, bundled Arduino BLE adapter, pure C++ LifeEngine/LocalBehavior/BleProtocol and Preferences-backed two-slot snapshot storage. Local life/preference survives via bounded versioned snapshots; transient animation boots idle. Feedback is explicit, context-specific and associated with one suggestion; eight source-tagged feedback episodes retained. Quiet-time routine uses learned preference after 60 seconds without direct activity, while sleep remains explicit. No wall-clock/offline elapsed time is fabricated.

BLE callbacks enqueue at most eight packets; one processed per loop. Main loop alone mutates animation/life, with local input priority. Attribute values only refresh when changed. Persistence writes on confirmed feedback, explicit forget and five-minute active-time checkpoint; not per frame. Snapshot checksum is an integrity check, not cryptographic authentication. NVS atomicity/power-loss and BLE resource/stability behavior require actual device testing. First boot checks key existence before reading empty NVS slots.

Additional host checks:

```sh
pio test -d firmware -e native -f test_life
```

BLE/Preferences are bundled with Arduino2.0.14; no new firmware dependency/version upgrade. Retain framework and bundled BLE license notices. Parent docs/roadmap/MVP-local.md owns exact protocol, software evidence and pending physical acceptance. Historic M01 observations above do not validate changed MVP behavior.

MVP build/upload and corrected first-boot startup PASS on LILYGO2026-10-11; owner confirmed LCD and both buttons. Final linker RAM47504 bytes, firmware964333 bytes. Short diagnostics are recorded in parent MVP record; physical BLE/learning/NVS recovery and long-run validation remain pending.

## S03 — v1 characterization baseline (2026-10-12)

`test/fixtures/v1_snapshots.h` freezes three 96-byte snapshots generated by the unchanged encoder at be58099: fresh; quiet/idle reject (source 1), then play/happy approve three times (source 2); all positive/negative cells at 1000 without episodes. Generation and active time remain zero. These are host fixtures, not device reads. The one-off generator lived at `/tmp/xuna_s03_generate.cpp`; its recipe is recorded in the fixture header. Do not regenerate fixtures just to make a changed codec pass.

```sh
pio test -d firmware -e native -f test_v1_characterization
pio test -d firmware -e native_asan -f test_v1_characterization
```

Coverage: snapshot fields and exact bytes; both contexts across 0..3 and 999..1001 evidence combinations including ties; command opcodes 0..7, lengths 0..16 and arguments 0..255, source bounds and error precedence; cache result codes 0..7, conflicts/clear/16-entry eviction; snapshot generation wrap/corruption; quiet routine rollover, idle gating and late ticks. Parser errors are 1..4; runtime result codes 5..7 belong to adapters/cache, not `parseCommand`.

Characterization and existing core/life suites PASS with C++11 warnings as errors plus ASan/UBSan. ESP32 target build PASS at 47,504 B static RAM / 964,333 B image, unchanged from S00. Production logic was unchanged in S03; paths/test runners subsequently migrated in S04. No firmware upload or physical BLE validation.

## S05 — Foundation primitives (2026-10-12)

Added pure C++11 fixed-point helpers, IRandom/Pcg32 and ClockModel under xuna_core; no main-loop/BLE/NVS wiring yet. `pio test -d firmware -e native` and `native_asan` now run four Unity suites (12 test cases). Clock ignores offline elapsed time after reboot until trusted sync, separates UTC from 64-bit monotonic time, rejects large rollback and flags forward gaps. Default 24 h/5 min thresholds remain Proposed tuning values.

PCG reference/adaptation source: [imneme/pcg-c-basic](https://github.com/imneme/pcg-c-basic/tree/bc39cd76ac3d541e618606bcc6e1e5ba5e5e6aa3), commit bc39cd76ac3d541e618606bcc6e1e5ba5e5e6aa3, pcg_basic.c (seeding/output/bounded sampling). Copyright 2014 Melissa O'Neill; Apache-2.0 notice retained in Random.h and full license at `lib/xuna_core/licenses/PCG-Apache-2.0.txt`. Adaptations: instance-based C++11 API and guarded zero bound, no global generator. Official seed 42/stream 54 vectors are cited in tests. This RNG is for reproducible behavior, not cryptographic use. No framework/dependency upgrade.

## S07 — In-memory events (2026-10-12)

Added event registry/envelope, boot-counter/sequence IDs and fixed-capacity RingBuffer/EventQueue. Five Unity suites now contain 18 test cases. Queues preserve FIFO, return explicit full rejection for critical learning/memory changes, count best-effort drops and drain within an event budget. All are single-owner core components; no BLE/ISR direct use, persistence codec or firmware wiring yet. Event.utcMinute and ClockModel compact minutes both use the planned 2020 epoch after S05-fix-01. ClockModel UTC millisecond input/output remains Unix time; zero compact minute is reserved unknown.

S05-fix-01 (2026-10-12): corrected compact-minute conversion and watermark reconstruction, added epoch bounds/overflow tests. No deployed state/protocol change because ClockModel remains unwired. Test suite now contains 19 cases.

## S06 — Identity core, partial (2026-10-12)

Added CRC32, 64-byte IdentityRecord codec and pure IdentityService with staged prepare/commit acceptance. Identity/corrupt-schema paths, migration, hatch origins and same-boot trusted birth-time tests run in the new test_identity suite; six suites total 23 cases. No NVS boot counter, real entropy, GPIO hold gesture, egg rendering, identity storage or main-loop integration is enabled yet. Full S06/board acceptance remains pending S01. Caller must provide real entropy and committed unique boot counter, then call acceptPersisted only after successful storage commit.

## Historical static LCD scene — 2026-10-12

Owner requested displaying the provided concept as one static image. Standard T-Display-S3 LCD Setup206/ST7789, parallel GPIO mapping, framework/driver pins and rotation 1 remain unchanged: physical 170×320, logical 320×170 landscape. PCB revision is still unknown. The PNG is a concept scene, not an animation sprite.

- Source: `assets/source/xuna_and_bg.png` (1721×914, owner-provided).
- Converter: `tools/convert_scene.py`, using existing Pillow; no dependency install.
- Output: `include/assets/XunaScene.h`, `src/assets/XunaScene.cpp`, and preview/metadata under `assets/generated/`.
- Regenerate: `python3 firmware/tools/convert_scene.py` from root, or `python3 tools/convert_scene.py` from firmware. Replace the source file and rerun; `--source` can select another input. Fit by uniform contain scale with nearest-neighbor and integer pixel rounding; pad if aspect differs, never crop. Target dimensions must match static assertions for this display.
- RGB565 uses canonical `(r>>3)<<11 | (g>>2)<<5 | b>>3`, with no BGR conversion/dithering. Setup206 config is TFT_RGB. Pinned TFT_eSPI's const pushImage reads one row from Flash; setSwapBytes(true) selects correct parallel tft_Write_16 path. Physical palette confirmation remains required.
- 54,400 uint16 pixels = 108,800 B; ELF symbol located in `.flash.rodata`, no whole-image stack/RAM allocation. Driver's temporary row is 640 B. Removed the old 80×80 RGB565 sprite allocation (12,800 B plus overhead). Asset is sent exactly once in DisplayManager::begin after init/rotation, with backlight enabled after transfer. Loop performs no display redraw. Existing pet/button/BLE/state/persistence logic continues without changing the static image; visual reactions are intentionally deferred in this owner-requested UI phase.

Build: `~/.platformio/penv/bin/pio run -d firmware -e xuna_t_display_s3` (from root). Linker RAM 46,072 B (−1,432 B), image 1,062,793 B (+98,460 B) relative to previous 47,504/964,333 baseline. Runtime heap/render timing cannot be inferred from those values. Asset regeneration is deterministic; native regression 23/23 PASS.

Upload: `~/.platformio/penv/bin/pio run -d firmware -e xuna_t_display_s3 -t upload --upload-port /dev/cu.usbmodem14101` (recheck port). Fresh NVS backup and actual partition match were recorded before this UI upload; no partition/schema change or erase_flash/NVS erasure. Root static-scene record owns final upload/runtime and pending visual acceptance results.

Static UI upload PASS on /dev/cu.usbmodem14101; owner confirmed correct image/colors/stability on 2026-10-12. Startup draw 31,663 us; no spontaneous reset during 25-second log observation. Post-upload NVS is byte-identical to fresh backup. Long-run/thermal and exact PCB revision remain unverified.

## Current animated room and pet (2026-10-12)

Owner reference `assets/source/xuna-pet.png` is retained. `tools/generate_pet_scene.py` traces/simplifies large character regions, discards texture/gradients, and authors expressions, five stable spots and separate short feet with dark tips. It does not use the resized reference as a complete animation sprite. Settings/palette are in `assets/scene_config.json`. All frames use the same 96×72 canvas and anchor (112,72); visible union height 65 px, about 38% of 170 px LCD. Room is a separate flat-color background with window/plant/ledge in the prior layout.

```sh
python3 firmware/tools/generate_pet_scene.py
~/.platformio/penv/bin/pio test -d firmware -e native
~/.platformio/penv/bin/pio test -d firmware -e native_asan
~/.platformio/penv/bin/pio run -d firmware -e xuna_t_display_s3
~/.platformio/penv/bin/pio run -d firmware -e xuna_t_display_s3 -t upload --upload-port /dev/cu.usbmodem14101
```

Generated background/frames/sheet/LCD PNG/GIF preview/metadata live in assets/generated; RGB565 header/source in include/assets/XunaAnimatedScene.h and src/assets/XunaAnimatedScene.cpp. Prior static export moved to assets/legacy, outside compiled src; old convert_scene.py is historical, use generate_pet_scene.py for current scene. Existing Pillow suffices; no dependency/framework/pin/partition upgrade.

Flash pixels: background 108,800 B + 20 frames × 13,824 B = 385,280 B. Single region buffer 13,824 B allocated at startup; no per-frame allocation or full-frame stack buffer. RGB565 magenta sentinel is composited against restored background in the fixed region; one opaque push covers previous/current bounds. TFT_RGB + setSwapBytes(true) retains canonical word handling for both Flash background and RAM region. Feet/torso overlap at joint to avoid a transparent seam; feet are independent immutable pieces, with explicit dark distal tips.

PetVisualAnimation uses unsigned timing: 1 px idle deformation, blink 3–5 s gaps and 100 ms stages, 2 px alternating foot bursts with 4–6.5 s gaps/160 ms steps. GPIO0 Happy displays distinct laughing eyes/open smile and immediate wiggle; GPIO14 Idle restores sly face. Since 2026-10-22 both buttons act on release (GPIO0 hold ≥ 800 ms = status card, both held 3 s = pairing QR, any press closes a card), and a GPIO14 ≥ 800 ms hold cycles the window scene (Auto/Day/Evening/Night/Rain, NVS `xuna_ui/scene`); reactions show a heart (pet) or puff (shoo) — see [device scenes](../docs/roadmap/device-scenes-2026-10-22.md). Sleep closes eyes and stops wiggle. Underlying button/BLE/learning/storage semantics preserved.

Final build/upload PASS: static RAM 46,144 B; image 1,340,569 B. Native and ASan/UBSan 25/25 PASS. Final 25-second serial run: 40 draws, max draw 4,754 us, max loop gap 4,767 us; heap stable, no spontaneous reset/error. Owner provisionally accepted the corrected version after reporting initial shape/visibility and seam/toe issues. Long-run stability/thermal and Android/BLE acceptance remain open. No claim of full validation from short observation.

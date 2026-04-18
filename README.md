# Air Quality Node

ESP32-based indoor air quality monitor with a 160×128 ST7735 TFT display, Wi-Fi provisioning portal, and a built-in web dashboard.

## Hardware

| Component | Detail |
|-----------|--------|
| MCU | ESP32 (ESP-IDF v6.0) |
| Display | ST7735 TFT 160×128 RGB565, SPI |
| Sensor (planned) | ENS160 (eCO2 / TVOC / AQI) |
| Temp & Humidity (planned) | Integrated with ENS160 module |

### Pin Map

| Signal | GPIO |
|--------|------|
| SPI MOSI | 23 |
| SPI CLK | 18 |
| CS | 5 |
| DC | 2 |
| RST | 4 |
| Backlight | Not connected |

## Project Structure

```
Air-Quality/
├── CMakeLists.txt              # Top-level project CMake
├── partitions.csv              # NVS 128 KB · PHY 4 KB · Factory 1.8 MB
├── sdkconfig                   # ESP-IDF Kconfig snapshot
│
├── main/
│   ├── main.c                  # Entry point → app_core_run()
│   ├── CMakeLists.txt          # Component registration & embedded files
│   ├── idf_component.yml       # espressif/mdns dependency
│   ├── B_hybrid_160x128.rgb565 # Embedded boot bitmap
│   │
│   ├── app/                    # ── Application layer ──
│   │   ├── app_core.c/h            # Main loop (50 fps tick)
│   │   ├── app_state_machine.c/h   # Boot phases → running state
│   │   └── dashboard_state.c/h     # Sensor data model & demo sweep
│   │
│   ├── drivers/                # ── Hardware abstraction ──
│   │   └── display_hal.c/h         # ST7735 SPI driver + framebuffer API
│   │
│   ├── services/               # ── Background services ──
│   │   ├── connectivity_service.c/h # Wi-Fi, SNTP, HTTP server, provisioning
│   │   ├── captive_dns.c/h          # DNS hijack for captive portal
│   │   └── memory_photo_service.c/h # NVS-backed photo storage (RGB565)
│   │
│   ├── ui/                     # ── User interface ──
│   │   ├── ui_flow.c/h             # Screen navigation & menu state machine
│   │   └── ui_renderer.c/h         # All screen rendering (fonts, gauges, panels)
│   │
│   └── web/                    # ── Embedded web frontend ──
│       ├── index.html
│       ├── app.css
│       └── app.js
│
└── managed_components/
    └── espressif__mdns/        # mDNS component (auto-fetched)
```

## Architecture

### Boot Sequence

```
POWER STABLE ──5 %──► DISPLAY READY ──18 %──► NETWORK STACK ──35 %──►
WIFI + NTP SYNC ──52 %──► CLOCK STATUS ──76/88 %──► DASHBOARD READY ──100 %──► RUNNING
```

Each phase renders a boot screen with progress bar, then transitions after a short delay. The `WIFI + NTP SYNC` phase calls `connectivity_service_setup_and_clock()` which blocks until the first clock sync attempt completes (up to 30 s).

### Runtime Loop

```
┌─────────────────────────────────────────────────────────┐
│  app_core_run()          main task · 20 ms tick         │
│                                                         │
│  ┌──────────────────┐   ┌────────────────────────────┐  │
│  │ app_state_machine │──►│ dashboard_state_build()    │  │
│  │     _tick()       │   │  · read sensors (or demo)  │  │
│  └────────┬─────────┘   │  · update clock            │  │
│           │              └────────────────────────────┘  │
│           ▼                                              │
│  ┌──────────────────┐   ┌────────────────────────────┐  │
│  │ ui_flow_tick()    │──►│ ui_renderer_draw_local_    │  │
│  │  · menu physics   │   │   screen()                 │  │
│  │  · smoke autoplay │   │  · monitor / wifi / alarm  │  │
│  └──────────────────┘   │  · game / memory / menu    │  │
│                          └─────────────┬──────────────┘  │
│                                        ▼                 │
│                          lcd_present_framebuffer()        │
└─────────────────────────────────────────────────────────┘
```

Sensor data refreshes every 1 s; the framebuffer is pushed to the display every 20 ms.

### Connectivity Service

```
┌──────────────────────────────────────────────────────┐
│  clock_sync_task          (FreeRTOS, prio 5)         │
│                                                      │
│  1. Init NVS, netif, Wi-Fi driver (once)             │
│  2. Load credentials (NVS → compile-time fallback)   │
│  3. Scan for target SSID                             │
│  4. Connect STA                                      │
│  5. SNTP sync (time.google.com · TZ = UTC+7)        │
│  6. Schedule next retry:                             │
│     · Success → 6 hours                              │
│     · Failure → 1 minute                             │
│                                                      │
│  Fallback: after 1 connect failure or 2 scan misses  │
│  ──► auto-start provisioning AP (AQNODE-XXXXXX)      │
│  ──► auto-probe STA reconnect every 60 s             │
│  ──► auto-close portal when WiFi returns              │
└──────────────────────────────────────────────────────┘

#### Wi-Fi Credential Testing (test-before-save)

When the user submits new Wi-Fi credentials via the web UI, the device does **not** save them immediately. Instead:

1. **Pre-scan** — scan nearby networks (without disconnecting current WiFi). If the target SSID is not visible → return "Network not found" instantly, zero disruption.
2. **Test connect** — disconnect current WiFi, switch to APSTA mode, attempt connection with the new credentials (15 s timeout, 3 retries).
3. **Success** — save to NVS → restart device.
4. **Failure** — restore original STA config and reconnect immediately (~2-5 s). Portal users get their AP restored.

Hidden networks skip the pre-scan step.

#### Auto-Reconnect After WiFi Loss

```
WiFi lost ──► 10 fast retries (event handler)
           │
           └─ all fail ──► 60 s timer ──► 1 sync cycle fail
                                          │
                                          └─► auto-open provisioning portal
                                              (s_provisioning_auto_opened = true)
                                              │
                                              └─► probe STA every 60 s
                                                  │
                                                  ├─ WiFi back ──► auto-close portal, STA mode
                                                  └─ WiFi still down ──► keep portal, retry
```

Manually opened portals (user clicks "Enable Setup Hotspot") do **not** auto-probe — they stay open until the user explicitly stops them.
```

### HTTP API

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/` | Web config UI |
| GET | `/app.css` | Stylesheet |
| GET | `/app.js` | Client JS |
| GET | `/api/state` | Wi-Fi / provisioning status JSON |
| GET | `/api/telemetry` | AQI, eCO2, TVOC, temp, humidity JSON |
| GET | `/api/scan` | Scan nearby Wi-Fi networks |
| POST | `/api/wifi` | Test & save Wi-Fi credentials (test-before-save) |
| POST | `/api/wifi/disconnect` | Disconnect current Wi-Fi (temporary) |
| POST | `/api/wifi/forget` | Erase saved credentials from NVS + open portal |
| POST | `/api/provisioning/start` | Enable AP provisioning hotspot (manual) |
| POST | `/api/provisioning/stop` | Stop provisioning portal + reconnect STA |
| GET | `/api/memory` | Memory photo status |
| POST | `/api/memory/photo` | Upload RGB565 photo (40 960 bytes) |
| DELETE | `/api/memory/photo` | Clear stored photo |

Captive portal endpoints (`/generate_204`, `/gen_204`, `/hotspot-detect.html`, etc.) redirect to `http://192.168.4.1/`.

### Display Screens

| Screen | Key | Content |
|--------|-----|---------|
| **Monitor** | `LOCAL_SCREEN_MONITOR` | Clock, AQI gauge, eCO2/temp/humidity cards |
| **Wi-Fi** | `LOCAL_SCREEN_WIFI` | SSID, link status, hostname, IP |
| **Alarm** | `LOCAL_SCREEN_ALARM` | Placeholder — two alarm slots |
| **Game** | `LOCAL_SCREEN_GAME` | Placeholder — reserved for mini-game |
| **Memory** | `LOCAL_SCREEN_MEMORY` | Show uploaded RGB565 photo or upload instructions |

A menu overlay with spring-physics highlight animation cycles through screens automatically during the smoke demo (triggered when AQI reaches level 5).

### Partition Table

| Name | Type | Offset | Size |
|------|------|--------|------|
| nvs | data/nvs | 0x9000 | 128 KB |
| phy_init | data/phy | 0x29000 | 4 KB |
| factory | app/factory | 0x30000 | 1.8 MB |

## Build & Flash

```bash
# Source ESP-IDF environment
source ~/.espressif/v6.0/esp-idf/export.sh

# Build
idf.py build

# Flash (auto-detect serial port)
idf.py flash

# Monitor serial output
idf.py monitor
```

Or use the VS Code tasks defined in `.vscode/tasks.json`.

## Network Access

Once connected to Wi-Fi the device is reachable at:

- **mDNS**: `http://aqnode.local`
- **IP**: shown on the Wi-Fi screen and in serial logs

If no credentials are configured (or connection repeatedly fails), the device creates a setup hotspot:

- **SSID**: `AQNODE-XXXXXX` (last 3 bytes of MAC)
- **Password**: `setup123`
- **Portal**: `http://192.168.4.1`

## Current Status

- [x] ST7735 display driver & framebuffer rendering
- [x] Boot sequence with progress bar
- [x] Dashboard with AQI gauge, clock, eCO2/temp/humidity
- [x] Wi-Fi STA with auto-reconnect & SNTP time sync
- [x] Provisioning portal (AP + captive DNS + web UI)
- [x] Wi-Fi credential test-before-save (pre-scan + APSTA test)
- [x] Auto-reconnect with STA probe during auto-opened portal
- [x] Wi-Fi management API (disconnect / forget / stop provisioning)
- [x] Memory photo upload/display via web
- [x] Menu system with animated highlight
- [x] mDNS (`aqnode.local`)
- [ ] Real sensor integration (ENS160 + AHT21)

## Wi-Fi Behavior Summary

| Scenario | What Happens |
|----------|-------------|
| Boot, no credentials | Portal opens, user enters WiFi via web UI |
| Boot, WiFi available | Connect → sync time → timer 6 h |
| Boot, WiFi unavailable | 1 fail cycle → auto-portal + probe every 60 s |
| Running, WiFi drops briefly | 10 fast retries → reconnect |
| Running, WiFi drops long | 10 retries → 60 s timer → auto-portal + probe |
| Auto-portal, WiFi returns | Probe succeeds → auto-close portal → STA mode |
| User opens portal manually | Portal stays until user stops it (no auto-probe) |
| User submits wrong WiFi | Pre-scan rejects unknown SSID instantly; wrong password restores original WiFi in ~5 s |
| User submits correct WiFi | Test OK → save NVS → restart |
| User clicks Disconnect | Temporary disconnect, reconnects on next timer |
| User clicks Forget | Erase NVS → portal opens for new credentials |
| User clicks Stop Provisioning | Close portal → trigger reconnect |
- [ ] Alarm functionality
- [ ] Mini-game implementation
- [ ] Button/input hardware integration

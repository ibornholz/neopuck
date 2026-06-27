# neopuck — Voice-Agent-Firmware für Waveshare ESP32-S3-Touch-AMOLED-1.75

Ein stylischer Voice-Puck: Knopf drücken, sprechen, der Agent antwortet per Sprache.
Backend-Endpoint (openclaw / eigener MCP-Agent / OpenAI-Realtime) frei konfigurierbar,
WLAN- und Agent-Konfiguration über das iPhone via BLE-Provisioning.

Das hier ist die **Applikationsschicht**. Die Board-Bring-up-Treiber (Display CO5300,
Touch CST9217, Codec ES8311, AEC ES7210, PMU AXP2101) kommen aus dem offiziellen
Waveshare-BSP — die App sitzt über einem schmalen `board.h`-Interface, damit der
Treiber-Layer austauschbar bleibt und nichts erfunden ist, was am echten Silizium
nicht stimmt.

---

## 1. Architektur

```
                 ┌────────────────────────────────────────────┐
                 │                  app_main                    │
                 │   State-Machine: BOOT→PROVISION→IDLE→        │
                 │   LISTENING→THINKING→SPEAKING→ERROR          │
                 └───┬───────────┬───────────┬───────────┬──────┘
                     │           │           │           │
              ┌──────▼───┐  ┌────▼─────┐ ┌───▼──────┐ ┌──▼─────────┐
              │   ui     │  │  audio   │ │  agent   │ │ provisioning│
              │ (LVGL)   │  │ pipeline │ │  client  │ │   (BLE)     │
              └────┬─────┘  └────┬─────┘ └────┬─────┘ └──┬──────────┘
                   │             │            │          │
              ┌────▼─────────────▼────────────▼──────────▼─────┐
              │                  board.h                        │
              │  bsp_display / bsp_touch / bsp_mic / bsp_spk    │
              │  bsp_pmu / bsp_button   (Waveshare-BSP dahinter)│
              └─────────────────────────────────────────────────┘
```

Zwei Cores werden ausgenutzt: LVGL/UI auf Core 0, Audio-Capture + WebSocket-RX auf
Core 1, damit das Rendering bei laufendem Stream nicht ruckelt.

---

## 2. Hardware-Map (verifiziert)

| Block        | Chip       | Bus        | Anmerkung                                |
|--------------|------------|------------|------------------------------------------|
| Display      | CO5300     | QSPI       | 466×466 AMOLED, true black = spart Strom |
| Touch        | CST9217    | I2C        | kapazitiv                                |
| Audio out    | ES8311     | I2S + I2C  | Speaker 8Ω/2W onboard                    |
| Audio in     | Dual-Mic   | I2S (TDM)  | über ES7210 mit Echo-Cancellation        |
| PMU          | AXP2101    | I2C        | Akku-Laden/-Management, Rails            |
| RTC          | PCF85063   | I2C        | optional für Timestamps                  |
| IMU          | 6-Achs     | I2C        | optional (Wake-on-Lift, Gesten)          |
| Buttons      | PWR + BOOT | GPIO       | PWR = Push-to-Talk, BOOT = lang→Settings |

Die echten Pin-/Clock-Werte stehen im Waveshare-BSP. `board.h` referenziert sie nur.

---

## 3. Bedien-Konzept (super simpel)

- **Idle**: tiefschwarzer Screen, mittig ein großer pulsierender Mic-Button,
  unten klein dein Firmenlogo, oben dezent WLAN-/Akku-Status.
- **Talk**: PWR-Button **oder** Tap auf den Mic-Button → `LISTENING`.
  Push-to-talk (halten) *oder* Toggle (tippen/tippen) — in der Config wählbar.
- **Listening**: animierter Audio-Ring reagiert auf den Pegel; live-Transkript
  läuft als Untertitel mit.
- **Thinking**: ruhiger Spinner, während der Agent antwortet.
- **Speaking**: Ring atmet im Takt der TTS, Antworttext scrollt mit.
- **Settings**: BOOT lang drücken → QR + BLE-Pairing-Screen fürs iPhone.

---

## 4. iPhone-Anbindung — was real geht

1. **Provisioning/Management über BLE**: Die Firmware nutzt den ESP-IDF
   `wifi_provisioning`-Manager (Transport BLE) plus zwei Custom-Endpoints
   (`agent-config`, `device-status`). Damit lassen sich vom iPhone aus setzen:
   WLAN-SSID/-Pass, Agent-URL, Agent-Token, Protokoll, Gerätename — und der
   Live-Status auslesen.
2. **Internet "übers iPhone"** = **Persönlicher Hotspot**. Du gibst im
   Provisioning den Hotspot-SSID/-Pass ein, das Device bucht sich ein. Es gibt
   bei iOS *keine* BLE/USB-Internetbrücke für Fremdgeräte — der Hotspot ist der Weg.
3. **App-Optionen**:
   - Sofort nutzbar: Espressif **"ESP BLE Provisioning"** (kostenlos im App Store),
     spricht genau diesen Provisioning-Manager. Reicht für SSID/Pass + Custom-Data.
   - Eigene App: ein schlanker SwiftUI-Client (CoreBluetooth) ist später ein
     separates Deliverable — Protokoll dafür steht unten in §6.

---

## 5. Build & Flash

Voraussetzung: ESP-IDF **v5.3+**, plus das Waveshare-BSP für dieses Board als
Component unter `components/waveshare_bsp/` (aus dem offiziellen Demo-Repo kopieren).

```bash
. $IDF_PATH/export.sh
idf.py set-target esp32s3
idf.py menuconfig      # PSRAM (octal), Flash 16MB, Partition = custom (partitions.csv)
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

PSRAM **muss** an sein (LVGL-Framebuffer + Audio-Buffer liegen im PSRAM).

---

## 6. Agent-Protokoll (frei konfigurierbar)

Default ist ein bewusst schlankes **JSON-über-WebSocket** + **Binärframes für Audio** —
genau das, was du mit einem eigenen openclaw/MCP-Backend selbst hostest. Ein
OpenAI-Realtime-Adapter ist als zweite Implementierung vorgesehen (`agent_proto`).

**Verbindung:** `wss://<host>/<path>?token=…` (Token alternativ als Header).

**Text-Frames = Steuerung (JSON):**

| Richtung | Beispiel | Bedeutung |
|----------|----------|-----------|
| C→S | `{"type":"session.start","sr":16000,"fmt":"pcm16","device":"neopuck-01"}` | Session-Init |
| C→S | `{"type":"input.begin"}` / `{"type":"input.end"}` | Mic-Fenster auf/zu |
| S→C | `{"type":"transcript","role":"user","text":"…","final":true}` | Live-Transkript |
| S→C | `{"type":"response.text","text":"…"}` | Antworttext (für Untertitel) |
| S→C | `{"type":"response.audio.begin"}` / `{"type":"response.audio.end"}` | TTS-Klammer |
| S→C | `{"type":"response.done"}` | Antwort fertig → IDLE |
| S→C | `{"type":"error","message":"…"}` | Fehler-Screen |

**Binär-Frames = Audio:** rohes **PCM16LE, 16 kHz, mono**.
C→S = Mikrofon (nach AEC), S→C = TTS-Playback. Chunkgröße 20 ms (640 Byte).

Backend frei: eigener Agent, openclaw, oder Adapter auf OpenAI Realtime / DeepSeek /
Doubao. Die State-Machine ist protokoll-agnostisch — du tauschst nur `agent_proto`.

---

## 7. Branding anpassen

- **Logo**: PNG → LVGL-C-Array (`lv_img`) und in `ui/assets_logo.c` ablegen, *oder*
  zur Laufzeit von SD/NVS laden (`/sdcard/logo.png`). Platzhalter ist drin.
- **Akzentfarbe**: `UI_ACCENT` in `ui/ui.h` (Default neonet-Purple `#7C3AED`).
- **Hintergrund**: bleibt true black `#000000` — auf AMOLED schöner *und* sparsamer.

---

## 8. Dateien

```
main/
  app_main.c          State-Machine, Event-Loop, Task-Setup
  app_state.h         Zustände, Events, geteilte Handles
  config_store.[ch]   NVS-Persistenz (WLAN, Agent-URL, Token, Optionen)
  provisioning.[ch]   BLE-Provisioning + Custom-Endpoints fürs iPhone
  agent_client.[ch]   WebSocket-Client + Protokoll-Handling
  audio_pipeline.[ch] Mic-Capture / Speaker-Playback über Ringbuffer
  ui/ui.[ch]          LVGL-Screens (Idle, Listening, Thinking, Speaking, Settings)
  board/board.h       BSP-Interface (Waveshare-Treiber dahinter)
```

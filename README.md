# neopuck — Voice-Agent-Firmware für Waveshare ESP32-S3-Touch-AMOLED-1.75

Ein stylischer Voice-Puck: Knopf drücken, sprechen, der Agent antwortet per Sprache.
Backend-Endpoint (openclaw / eigener MCP-Agent / OpenAI-Realtime) frei konfigurierbar,
WLAN- und Agent-Konfiguration app-frei über SoftAP + Captive Portal (QR auf dem Display).

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
              │ (Orb)    │  │ pipeline │ │  client  │ │ (SoftAP+QR) │
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

- **Idle**: tiefschwarzer Screen, mittig der **Orb** (violett, ruhig atmend),
  unten klein dein Firmenlogo, oben dezent WLAN-/Akku-Status.
- **Talk**: PWR-Taste **oder** Tap auf den Orb → `LISTENING` (Orb wird grün).
  Push-to-talk (halten) *oder* Toggle (tippen/tippen) — in der Config wählbar.
- **Listening**: der Orb pulsiert mit dem Mic-Pegel; live-Transkript oben.
- **Thinking**: Orb amber mit Punkt, während der Agent antwortet.
- **Speaking**: Orb cyan, atmet im Takt der TTS; Tap = „tap to dismiss" (Abbruch).
- **Settings**: BOOT lang drücken → Captive-Portal (QR) erneut öffnen.

---

## 4. Provisioning — app-frei (SoftAP + Captive Portal)

1. **Kein App nötig** (iPhone *und* Android): Unkonfiguriert öffnet neopuck das
   offene WLAN **`neopuck-setup`** und zeigt dafür einen **QR** auf dem AMOLED.
   Handy verbindet → Captive-Portal-Popup → dunkle Config-Seite (`192.168.4.1`).
2. **Eingabe**: WLAN (Liste via `/scan`), Agent-URL, Agent-Token. `POST /save`
   reicht alles als JSON an `config_apply_json()`; danach verbindet sich das Gerät.
3. **Internet "übers iPhone"** = **Persönlicher Hotspot**: einfach den Hotspot als
   WLAN eintragen.
4. **Re-Provisioning**: BOOT lang → Portal öffnet erneut. Details in `IPHONE.md`.

---

## 5. Build & Flash

Voraussetzung: ESP-IDF **v5.3+**. Der Board-Treiber-Layer liegt als Component unter
`components/waveshare_bsp/` (CO5300 + CST9217 + ES8311/ES7210 + AXP2101 über die
Registry-Treiber, angebunden via `esp_lvgl_port`). Registry-Treiber + LVGL v9 zieht
der Component-Manager beim ersten Build automatisch.

```bash
. $IDF_PATH/export.sh
./scripts/build.sh         # set-target + build + merge -> dist/neopuck-merged.bin
./scripts/flash.sh         # Port auto; sonst --port /dev/cu.usbmodemXXXX
idf.py -p <port> monitor   # Logs
```

`./scripts/pack.sh` macht build + flash in einem Schritt. PSRAM (octal) ist in
`sdkconfig.defaults` an; die LVGL-Zeichenpuffer liegen aus DMA-Gründen im internen
RAM (klein, partiell), Audio-/Instruktions-Caches im PSRAM.

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

# CLAUDE.md — Projekt neopuck (Build-Brief für Claude Code)

Du arbeitest an **neopuck**: einer Voice-Agent-Firmware für das
**Waveshare ESP32-S3-Touch-AMOLED-1.75** (ESP-IDF v5.3+). Knopf drücken, sprechen,
ein konfigurierbarer Agent antwortet per Sprache; zusätzlich kann der Agent über
Tools kleine Programme/Spiele auf dem Device starten.

Dieses Dokument ist dein Auftrag. Arbeite die Tasks **in Reihenfolge** ab,
verifiziere jeden Meilenstein mit Build/Flash, und committe in logischen Schritten.
Frag nur nach, wenn ein Schnittstellen-Vertrag wirklich unklar ist.

---

## Hardware (verifiziert — nicht raten)

- MCU ESP32-S3R8, 8MB PSRAM (octal), 16MB Flash, 240 MHz, WiFi 2.4G + BLE5
- Display 466×466 AMOLED, Treiber **CO5300**, QSPI
- Touch **CST9217**, I2C
- Audio out **ES8311** (+ Speaker 8Ω/2W onboard); Audio in Dual-Mic über **ES7210** (AEC)
- PMU **AXP2101**, RTC PCF85063, 6-Achs-IMU
- Type-C = nativer ESP32-S3-USB (Flash + Log), automatische Download-Schaltung
- Buttons: PWR (Push-to-Talk) und BOOT (lang → Settings; gehalten beim Power-on → Download-Modus)

---

## Aktueller Stand des Repos

Vorhanden ist die **Applikationsschicht** (rund 1100 Zeilen), die über einem
schmalen Board-Interface sitzt:

```
main/app_main.c          State-Machine + Event-Loop  (fertig)
main/app_state.h         Zustände/Events             (fertig)
main/config_store.[ch]   NVS-Config + config_apply_json()  (fertig)
main/provisioning.[ch]   AKTUELL BLE — wird in Task 3 durch SoftAP ersetzt
main/agent_client.[ch]   WebSocket + Protokoll       (fertig, Default-Proto)
main/audio_pipeline.[ch] Mic→Agent / TTS→Speaker     (fertig)
main/ui/ui.[ch]          AKTUELL Mic-Button — wird in Task 2 zum Orb-Design
main/ui/assets_logo.c    Logo-Platzhalter
main/board/board.h       BSP-INTERFACE — Implementierung fehlt (Task 1)
tools/flasher/flash.py   Flasher (fertig)
scripts/{build,flash,pack}.sh   Build-/Flash-Pipeline (fertig)
```

**Was fehlt, damit es baut und flasht:** der Board-Treiber-Layer (Task 1). Erst
danach produziert `./scripts/pack.sh` ein echtes Image. Tasks 2–4 sind Features.

---

## Build / Flash / Verify

```bash
. $IDF_PATH/export.sh
idf.py set-target esp32s3
# sicherstellen: PSRAM octal an, Flash 16MB, custom partitions (partitions.csv)
#   -> steht in sdkconfig.defaults; mit `idf.py menuconfig` gegenprüfen
./scripts/build.sh         # build + merge -> dist/neopuck-merged.bin
./scripts/flash.sh         # Port wird auto-erkannt; sonst --port /dev/cu.usbmodemXXXX
idf.py -p <port> monitor   # Logs
```

Wenn esptool bei „Connecting…" hängt: BOOT halten, neu einstecken/Reset, erneut.

---

## Task 1 — Board-BSP + `board.h`-Adapter  (Pflicht, zuerst)

Ziel: das vorhandene Scaffold baut und flasht, der AMOLED zeigt die UI.

1. Hol Waveshares offizielles **ESP-IDF-Demo** für *genau dieses* Board
   (ESP32-S3-Touch-AMOLED-1.75) vom Board-Wiki bzw. deren GitHub. Lege deren
   Board-/Treiber-Code als Component unter `components/waveshare_bsp/` ab.
2. **Inspiziere die echte API** des Demos (Header in deren `components/`). Rate
   keine Funktionsnamen. Schreibe dann `components/board_adapter/` (oder in der
   BSP-Component), das die Calls des Demos auf das Interface in
   `main/board/board.h` mappt. Der Vertrag von `board.h` ist **stabil** — die
   App hängt daran; ändere lieber den Adapter als das Interface.
3. `board.h` verlangt konkret:
   - `bsp_display_start()` → CO5300 + CST9217 + `esp_lvgl_port` initialisieren,
     `lv_display_t*` liefern. `bsp_display_lock/unlock` über den lvgl_port-Mutex.
   - `bsp_audio_init(16000)` + `bsp_mic_read()` (PCM16 16k mono nach ES7210-AEC)
     + `bsp_spk_write()` (ES8311) + `bsp_spk_volume_set()`.
   - `bsp_buttons_init(cb)` → PWR/BOOT auf DOWN/UP/LONG mappen.
   - `bsp_battery_percent()` / `bsp_is_charging()` über AXP2101.
4. **Meilenstein 1:** `./scripts/pack.sh` läuft durch, Device bootet, UI sichtbar,
   Buttons feuern Events (im Monitor sichtbar). Erst danach weiter.

> Hinweis: Sollte ein Waveshare-Treiber nur in Arduino-Form vorliegen, nimm
> stattdessen die passenden Espressif-Componenten (`esp_lcd` + CO5300-Panel-Treiber,
> `esp_lvgl_port`, `es8311`/`es7210` aus dem `esp-bsp`/Component-Registry) und
> verdrahte die Pins gemäß Waveshare-Schaltplan. Pins NUR aus Schaltplan/BSP, nie erfinden.

---

## Task 2 — UI auf „leuchtender Orb" umbauen  (`main/ui/ui.c`, `ui.h`)

Ersetze die Mic-Button-Metapher durch einen **leuchtenden Orb, dessen Farbe den
Zustand kodiert**. True-black Hintergrund bleibt (`#000000`).

Layout (rund, 466×466, alles auf den Kreis komponiert):
- **oben**: Transkript/Antworttext, mehrzeilig, zentriert, dezenter Weißton.
- **Mitte**: der Orb — heller Kern + konzentrische Glow-Schichten (in LVGL über
  `lv_obj` mit `shadow_width`/`shadow_color` ODER gestapelte Kreise mit fallender
  Opazität) + dünner Ring drumherum.
- **darunter**: Status-Label in Versalien, leicht gesperrt (letter-spacing).
- **darunter**: Hint-Zeile, kursiv, gedimmt.

Zustandsfarben (Orb + Ring + Label):
| State | Farbe | Hint |
|-------|-------|------|
| IDLE | Akzent `#7C3AED`, ruhig atmend | — |
| LISTENING | Grün `#34D399`, Kern `#EAFFF4` | „release to send" |
| THINKING | Amber `#F5A623`, dünner Ring + Punkt | — |
| SPEAKING | Cyan `#38BDF8`, Kern `#CBEEFF` | „tap to dismiss" |
| ERROR | Rot `#FF3B30` | „Verbindung gestört" |

Weitere Konstanten: Text `#F2F2F7`, dim `#8A8A8E`. Akzent/Logo bleiben themebar
über `ui.h`. **Interrupt:** Tap im SPEAKING-Zustand = Wiedergabe abbrechen
(`audio_play`-Queue leeren) und zurück auf IDLE — verdrahte das als „tap to dismiss".
Logo klein unten, Opazität ~80%.

LVGL ist nicht thread-safe: jeder UI-Zugriff aus Fremd-Tasks via `bsp_display_lock()`.

---

## Task 3 — App-freies Provisioning: SoftAP + Captive Portal  (`provisioning.c`)

Ersetze BLE als **Default** durch SoftAP + Captive Portal (iPhone **und** Android,
ganz ohne App). BLE darf optional als zweiter Pfad bleiben, ist aber nicht nötig.

Flow:
1. Bei `!config_is_provisioned()` → SoftAP **`neopuck-setup`** (offen) starten,
   und auf dem AMOLED einen **QR** zeigen, der das Setup-WLAN encodiert
   (`WIFI:T:nopass;S:neopuck-setup;;`). Fallback-Text: `http://192.168.4.1`.
2. Captive-Portal-DNS: kleiner DNS-Responder, der **alle** Anfragen auf
   `192.168.4.1` auflöst, damit das Portal-Popup erscheint.
3. `esp_http_server` mit:
   - `GET /` → dark-themed Config-Seite (Marke: schwarz/violett, passend zur UI).
   - `GET /scan` → JSON der `esp_wifi_scan_get_ap_records()` (SSID + RSSI + lock).
   - `POST /save` → Felder `wifi_ssid, wifi_pass, agent_url, agent_token` →
     in ein JSON packen und an **`config_apply_json()`** geben (existiert schon).
4. Nach Save: AP + DNS + HTTP stoppen, STA mit gespeicherten Creds verbinden,
   `EV_WIFI_UP`/`EV_PROV_DONE` setzen (wie im bisherigen Flow). Re-Provisioning
   über BOOT-lang (`EV_SETTINGS`) öffnet den Portal-Modus erneut.

Sicherheit: keine Creds in URL-Parametern; nur `POST`-Body. QR encodiert NICHT das
Heim-WLAN-Passwort (kennt das Device nicht) — nur den Join ins Setup-Netz.

Für den QR auf dem Screen: kleine QR-Lib einbinden (z.B. `espressif/qrcode` aus der
Component-Registry) und das Muster in ein LVGL-Canvas/`lv_qrcode` rendern.

---

## Task 4 — Mini-App-Runtime: Agent startet Programme  (`app.launch`)

Der Agent kann über ein Tool eine kleine On-Device-App starten. Auf dem Device
ist das eingehende Steuerung über den bestehenden WebSocket.

1. In `agent_client.c` zwei Nachrichtentypen ergänzen:
   - `{"type":"app.launch","app":"<id>","params":{…}}`
   - `{"type":"app.exit"}`
   Beide als neue `EV_*`-Bits an die State-Machine reichen (App-ID/params in einer
   kleinen Queue/Struct übergeben).
2. Neuer Zustand **`ST_MINIAPP`** in `app_state.h` + `app_main.c`. Während aktiv:
   Voice-States pausieren; `app.exit` oder ein Exit-Touch → zurück zu `ST_IDLE`.
3. `main/miniapps/` mit einer Registry:
   ```c
   typedef struct {
     const char *id;
     void (*launch)(lv_obj_t *parent, const char *params_json);
     void (*tick)(uint32_t dt_ms);
     void (*on_touch)(lv_point_t p);
     void (*exit)(void);
   } miniapp_t;
   ```
   Registriere die Apps in einem Array; `app.launch` sucht per `id`.
4. Liefere **eine** Beispiel-App, damit der Flow testbar ist — z.B. einen simplen
   „tap to dive"-Minigame-Screen oder einen neutralen Timer/Clock. Bewusst klein.
5. Die Agent-Seite (MCP-Tool, das `app.launch` über den Socket schickt) ist NICHT
   Teil dieses Repos — hier nur das Empfangen/Ausführen auf dem Device.

---

## Protokoll (für Agent-Client — nicht neu erfinden)

WebSocket: Text-Frames = JSON-Steuerung, Binär-Frames = **PCM16LE 16k mono** Audio.
C→S: `session.start`, `input.begin`, `input.end`, Audio binär.
S→C: `transcript`, `response.text`, `response.audio.begin/end`, `thinking`,
`response.done`, `error`, sowie neu `app.launch`/`app.exit`. Audio S→C binär = TTS.
Backend ist frei konfigurierbar (`config.agent_url`/`token`); ein OpenAI-Realtime-
Adapter ist als zweite Proto-Variante vorgesehen (`PROTO_OPENAI_RT`), aber optional.

**Offen (nicht blockierend):** Der konkrete „openclaw"-Backend-Vertrag ist noch
nicht final. Default-Proto bleibt aktiv, bis das geklärt ist.

---

## Constraints & Gotchas

- PSRAM (octal) MUSS an sein — LVGL-Framebuffer + Audio-Puffer liegen dort.
- Hintergrund true black `#000000` (AMOLED: schöner + sparsamer).
- LVGL nicht thread-safe → immer `bsp_display_lock()` aus Fremd-Tasks.
- Audio-Capture + WS-RX auf Core 1 pinnen, UI/LVGL auf Core 0 (schon so angelegt).
- Pins/Clocks ausschließlich aus Waveshare-Schaltplan/BSP, nie erfinden.
- Gemergtes Image wird bei `0x0` geflasht (macht `merge-bin`/`flash.py` schon).
- Keine sensiblen Daten in URLs/Query-Strings.

## Definition of Done

`./scripts/pack.sh` baut und flasht ohne Fehler; Device bootet ins Captive-Portal,
lässt sich per QR/Handy ins WLAN bringen, verbindet sich danach selbst, Voice-Flow
läuft mit Orb-UI (grün/amber/cyan), und ein per `app.launch` gestarteter Mini-App-
Screen erscheint und lässt sich wieder verlassen.

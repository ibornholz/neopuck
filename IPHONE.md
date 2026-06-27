# iPhone-Anbindung — Provisioning & Management

> Seit der SoftAP-Umstellung (Task 3) wird **keine App** mehr gebraucht — weder auf
> iPhone noch Android. Provisioning läuft komplett über ein Captive Portal im Browser.

## Setup in 4 Schritten (App-frei)

1. neopuck einschalten. Ist es noch nicht konfiguriert, öffnet es ein offenes WLAN
   **`neopuck-setup`** und zeigt auf dem AMOLED einen **QR-Code** dafür.
2. QR mit der iPhone-Kamera scannen (oder in den WLAN-Einstellungen `neopuck-setup`
   wählen). Das iPhone tritt dem Setup-Netz bei.
3. Das **Captive-Portal-Popup** erscheint automatisch (sonst `http://192.168.4.1`
   im Browser öffnen). Die dunkle Config-Seite lädt.
4. Felder ausfüllen und **Speichern**:
   - **WLAN-Name / -Passwort** — dein Heim-WLAN (oder iPhone-Hotspot). Die Liste
     unter dem SSID-Feld wird per `/scan` mit sichtbaren Netzen gefüllt.
   - **Agent-URL** — z.B. `wss://agent.neonet.ai/voice`
   - **Agent-Token** — optional (Bearer)

Danach stoppt neopuck den SoftAP, verbindet sich mit dem WLAN und dem Agent und
springt in den Voice-Flow (Orb-UI).

## Re-Provisioning

**BOOT-Taste lang drücken** (> 0,8 s) öffnet das Portal jederzeit wieder
(`EV_SETTINGS` → `provisioning_reopen()`).

## Sicherheit

- Credentials werden **nur im POST-Body** übertragen, nie in URL/Query.
- Der QR-Code encodiert ausschließlich den Join ins **offene Setup-Netz**
  (`WIFI:T:nopass;S:neopuck-setup;;`) — niemals das Heim-WLAN-Passwort.

## Schnittstelle (für eigene Tools)

Das Portal stellt bereit:

| Route        | Methode | Zweck                                                    |
|--------------|---------|---------------------------------------------------------|
| `/`          | GET     | dunkle Config-Seite                                     |
| `/scan`      | GET     | JSON der sichtbaren APs: `[{ssid,rssi,lock}, …]`        |
| `/save`      | POST    | `wifi_ssid, wifi_pass, agent_url, agent_token` (form)   |

`/save` reicht die Felder als JSON an `config_apply_json()` weiter — derselbe
Config-Pfad wie zuvor. Wer mag, kann `/save` also auch per `curl` bedienen.

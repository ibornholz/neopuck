# neopuck-flash — Tool zum Bespielen des Devices

Ein kleiner Flasher, der die komplette Firmware in einem Rutsch aufs
ESP32-S3-Device packt. Drei Wege, je nach Situation:

| Befehl | macht |
|--------|-------|
| `./scripts/build.sh` | Firmware bauen + zu einem Image mergen (`dist/neopuck-merged.bin`) |
| `./scripts/flash.sh` | gemergtes Image aufs Device flashen (Port auto) |
| `./scripts/pack.sh`  | **bauen + flashen in einem** — der „alles drauf"-Befehl |

## Voraussetzungen

- Zum **Bauen**: aktive ESP-IDF v5.3+ (`. $IDF_PATH/export.sh`).
- Zum **reinen Flashen** (z.B. auf einem zweiten Rechner ohne IDF): nur
  `pip install -r tools/flasher/requirements.txt` — das zieht `esptool`.

## Schnellstart

```bash
# einmalig: ESP-IDF aktivieren
. $IDF_PATH/export.sh

# alles drauf:
./scripts/pack.sh
```

Das Tool sucht den Port selbst (`usbmodem` / `ttyACM` / …). Bei mehreren Geräten
fragt es nach. Festen Port erzwingen:

```bash
./scripts/flash.sh --port /dev/cu.usbmodem1101
```

Flash vorher komplett löschen (bei hartnäckigen Zuständen):

```bash
./scripts/flash.sh --erase
```

## Wenn der Flash hängt

Bleibt esptool bei `Connecting…` stehen, ist die native USB meist durch ein
abgestürztes Programm blockiert: **BOOT-Taste gedrückt halten, Device neu
einstecken (oder Reset), dann Befehl erneut**. Damit erzwingst du den
Download-Modus; danach läuft die automatische Download-Schaltung wieder.

## Firmware verteilen ohne IDF

`dist/neopuck-merged.bin` ist ein vollständiges Image ab Offset `0x0`. Du kannst
es weitergeben; auf dem Zielrechner reicht `esptool` + `flash.py --bin …`. Damit
kann auch jemand ohne Toolchain ein fertiges Device bespielen.

#!/usr/bin/env python3
"""
neopuck-flash — packt die komplette Firmware in einem Rutsch aufs Device.

Flasht ein gemergtes Image (bootloader + partition-table + app) bei 0x0 auf den
ESP32-S3 über dessen nativen USB-Port. Port wird automatisch erkannt; bei mehreren
Kandidaten wird gefragt. Braucht nur 'esptool' (pip), keine laufende ESP-IDF.

Beispiele:
    python3 flash.py                      # auto: dist/neopuck-merged.bin, Port auto
    python3 flash.py --port /dev/cu.usbmodem1101
    python3 flash.py --bin build/foo.bin --erase
"""
import argparse
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
DEFAULT_BIN = REPO / "dist" / "neopuck-merged.bin"

# Marker, an denen man den ESP32-S3-USB-Port plattformübergreifend erkennt.
PORT_HINTS = ("usbmodem", "usbserial", "wchusbserial", "ttyACM", "ttyUSB")

C_OK = "\033[92m"
C_WARN = "\033[93m"
C_ERR = "\033[91m"
C_DIM = "\033[90m"
C_END = "\033[0m"


def log(msg, color=""):
    print(f"{color}{msg}{C_END}")


def find_ports():
    try:
        from serial.tools import list_ports
    except ImportError:
        log("pyserial fehlt — 'pip install esptool' installiert es mit.", C_WARN)
        return []
    found = []
    for p in list_ports.comports():
        if any(h in (p.device or "") for h in PORT_HINTS):
            found.append(p.device)
    return found


def pick_port(explicit):
    if explicit:
        return explicit
    ports = find_ports()
    if not ports:
        log("Kein Device-Port gefunden. Stecker prüfen, oder --port angeben.", C_ERR)
        log("Tipp: BOOT halten + neu einstecken erzwingt den Download-Modus.", C_DIM)
        sys.exit(2)
    if len(ports) == 1:
        log(f"Port erkannt: {ports[0]}", C_DIM)
        return ports[0]
    print("Mehrere Ports gefunden:")
    for i, p in enumerate(ports):
        print(f"  [{i}] {p}")
    idx = input("Welcher? [0]: ").strip() or "0"
    try:
        return ports[int(idx)]
    except (ValueError, IndexError):
        log("Ungültige Auswahl.", C_ERR)
        sys.exit(2)


def run_esptool(args):
    """esptool als Modul aufrufen — robust gegen PATH-Probleme."""
    cmd = [sys.executable, "-m", "esptool"] + args
    log(f"$ {' '.join(cmd)}", C_DIM)
    return subprocess.run(cmd).returncode


def main():
    ap = argparse.ArgumentParser(description="neopuck Firmware-Flasher")
    ap.add_argument("--port", help="Serieller Port (Default: auto-detect)")
    ap.add_argument("--bin", default=str(DEFAULT_BIN), help="Gemergtes .bin")
    ap.add_argument("--baud", default="921600", help="Flash-Baudrate")
    ap.add_argument("--erase", action="store_true", help="Flash vorher löschen")
    args = ap.parse_args()

    binpath = Path(args.bin)
    if not binpath.exists():
        log(f"Image fehlt: {binpath}", C_ERR)
        log("Erst bauen:  ./scripts/build.sh   (oder ./scripts/pack.sh)", C_DIM)
        sys.exit(1)

    port = pick_port(args.port)
    base = ["--chip", "esp32s3", "--port", port, "--baud", args.baud]

    if args.erase:
        log("Lösche Flash …", C_WARN)
        if run_esptool(base + ["erase_flash"]) != 0:
            log("Erase fehlgeschlagen.", C_ERR)
            sys.exit(1)

    log(f"Flashe {binpath.name} → {port}", C_OK)
    rc = run_esptool(base + [
        "--before", "default_reset", "--after", "hard_reset",
        "write_flash", "--flash_size", "16MB", "0x0", str(binpath),
    ])

    if rc != 0:
        log("\nFlash fehlgeschlagen.", C_ERR)
        log("Wenn er bei 'Connecting…' hängt: BOOT gedrückt halten, neu "
            "einstecken/Reset, dann nochmal.", C_DIM)
        sys.exit(1)

    log("\nFertig. Device startet neu.", C_OK)
    log("Logs ansehen:  idf.py -p %s monitor" % port, C_DIM)


if __name__ == "__main__":
    main()

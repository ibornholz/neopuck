# neopuck ⇄ OpenAI-Realtime Bridge

Macht aus dem neopuck einen sprechenden Dialog-Assistenten: das Device streamt
Mikrofon-Audio an diese Bridge, die Bridge spricht mit der **OpenAI Realtime API**
(Verstehen + Antworten + Stimme) und schickt das TTS-Audio zurück aufs Device.
**openclaw** und die **Device-Mini-Apps** hängen als Tools dran.

```
neopuck ──ws, PCM16 16k──►  Bridge  ──wss──►  OpenAI Realtime
        ◄──PCM16 16k TTS──          ◄──────   + Tools: ask_openclaw, show_qr,
                                               start_stopwatch, close_app
```

## Setup

```bash
cd bridge
python3 -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt
cp .env.example .env          # OPENAI_API_KEY eintragen (+ optional openclaw)
python3 neopuck_openai_bridge.py
```

Die Bridge lauscht auf `ws://0.0.0.0:8765`.

## Device verbinden

Im neopuck Captive-Portal (BOOT lang → `neopuck-setup` → `192.168.4.1`) als
**Agent-URL** die Bridge eintragen, erreichbar im selben WLAN:

```
ws://<ip-des-bridge-rechners>:8765
```

`<ip>` z.B. per `ipconfig getifaddr en0`. Optional `NEOPUCK_TOKEN` setzen und im
Portal als Agent-Token eintragen.

Dann am Device den **Orb halten**, sprechen, **loslassen** → Antwort kommt als
Sprache zurück. „Zeig mir einen QR für example.com" → das Modell ruft `show_qr`
auf, der QR erscheint auf dem Display.

## Architektur-Entscheidung

OpenAI Realtime ist hier das **Gehirn + die Stimme** (beste Live-Dialog-Qualität,
passt 1:1 auf neopucks Audio-Protokoll). **openclaw ist als Tool** eingebunden:
fragt das Modell `ask_openclaw`, geht ein HTTP-Call an euer Backend.

> **Anpassen:** Der genaue openclaw-Request/Response liegt in `ask_openclaw()`
> (aktuell generischer `POST {"message": …}` mit Basic-Auth). Sag mir den echten
> Endpoint/das Schema, dann verdrahte ich es fest. Wer openclaw stattdessen als
> *Gehirn* will (OpenAI nur für STT/TTS), kann das — dann anders verschalten.

## Tools (Device-Fähigkeiten)

| Tool              | Wirkung am Device                         |
|-------------------|-------------------------------------------|
| `show_qr`         | `app.launch qr` — QR aus `{data,caption}` |
| `start_stopwatch` | `app.launch stopwatch`                    |
| `close_app`       | `app.exit`                                |
| `ask_openclaw`    | HTTP → openclaw, Antwort zurück ans Modell|

Neue Fähigkeit = neue Mini-App im Device (`main/miniapps/`) **und** ein Eintrag in
`TOOLS` + `run_tool()` hier.

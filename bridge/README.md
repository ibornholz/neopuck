# neopuck Voice-Bridge

Verbindet die neopuck-Hardware mit **openclaw als Gehirn** und einer Sprach-Engine.
Der Puck kann openclaws Browser-App (Kite) nicht ausführen, also übernimmt die
Bridge dieselbe Aufgabe „ohne Browser": Audio rein/raus zwischen Puck und openclaw.

```
neopuck ──ws, PCM16 16k──►  Bridge  ──►  STT ─► openclaw (Gehirn) ─► TTS
        ◄──PCM16 16k TTS──          ◄───────────────────────────────────
                                    + Device-Tools: show_qr / stopwatch / close_app
```

- **Device ↔ Bridge:** eigenes WS-Protokoll (PCM16 16k + JSON). Auth = ein
  `agent_token` (Bearer, optional).
- **Bridge ↔ openclaw:** openclaw-Gateway (`kite/ws`) mit **Ed25519-Geräte-Pairing**
  (`openclaw_client.py`). Die Bridge meldet sich wie ein normales openclaw-Gerät an
  und wird **einmal freigegeben**. Kein Umbau am App-Token, kein neuer Token in
  openclaw nötig.

## Sprach-Weg (wird final verdrahtet)
openclaw bietet `chat.send` (+ `sessions.messages.subscribe`) für Text und eine
**`talk.*`-API** (`talk.session.create/appendAudio/...`) für Audio. Sobald das Gerät
gepairt ist, wird live geprüft, ob `talk.*` direkt Audio liefert (dann **kein
eigenes OpenAI nötig**) oder ob STT/TTS über OpenAI laufen — und entsprechend
verdrahtet.

## Schnellstart (lokal)
```bash
python3 -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt
cp .env.example .env          # openclaw-Token etc. eintragen
python3 neopuck_openai_bridge.py
```
Beim ersten Start loggt die Bridge ihre `deviceId` → in openclaw als **operator**
freigeben. Server-Deployment + Update-Strategie: siehe **DEPLOY.md**.

## Device verbinden
Im neopuck Captive-Portal (BOOT lang → `neopuck-setup` → `192.168.4.1`) als
Agent-URL die Bridge eintragen: `ws://<bridge-ip>:8765` (lokal) bzw.
`wss://new.bornholz.com/neopuck-bridge` (Server, hinter TLS).

## Dateien
| Datei | Zweck |
|-------|-------|
| `openclaw_client.py` | openclaw-Gateway-Client (Ed25519-Pairing-Handshake) |
| `neopuck_openai_bridge.py` | WS-Server fürs Device + Sprach-Pipeline |
| `audio_util.py` | PCM16-Resampler 16k↔24k (stdlib) |
| `Dockerfile` / `docker-compose.yml` / `*.service` | Deployment |
| `DEPLOY.md` | Server-Setup, TLS, Updates |

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

## Sprach-Weg: openclaw macht alles (kein OpenAI nötig)
openclaw liefert über `talk.session.create` (mode `stt-tts`, transport
`gateway-relay`, brain `agent-consult`, provider `microsoft`) den kompletten
Voice-Loop **server-seitig**: Audio rein via `talk.session.appendAudio`, Audio +
Transkripte raus via `talk.event`. Die Standard-Bridge `neopuck_openclaw_bridge.py`
relayed nur PCM16 — **kein eigener OpenAI-Key**.
(Alternative: `neopuck_openai_bridge.py` nutzt OpenAI Realtime; braucht einen Key.)

## Schnellstart (lokal)
```bash
python3 -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt
cp .env.example .env          # openclaw-Token etc. eintragen
OCLAW_DEBUG=1 python3 neopuck_openclaw_bridge.py   # DEBUG zeigt talk.event-Felder
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
| `neopuck_openclaw_bridge.py` | **Standard**: PCM-Relay Device ⇄ openclaw talk.* |
| `neopuck_openai_bridge.py` | Alternative: Device ⇄ OpenAI Realtime |
| `audio_util.py` | PCM16-Resampler 16k↔24k (stdlib) |
| `Dockerfile` / `docker-compose.yml` / `*.service` | Deployment |
| `DEPLOY.md` | Server-Setup, TLS, Updates |

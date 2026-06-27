#!/usr/bin/env python3
"""
neopuck ⇄ OpenAI-Realtime Bridge
================================

Verbindet das neopuck-Device (eigenes WebSocket-Protokoll) mit der OpenAI
Realtime API (Sprache rein/raus = das "Gehirn" + die Stimme). openclaw und die
Device-Mini-Apps hängen als *Tools* dran, die das Modell im Gespräch aufrufen kann.

    neopuck  ──(ws, PCM16 16k)──►  diese Bridge  ──(wss)──►  OpenAI Realtime
             ◄──(PCM16 16k TTS)──                ◄────────   (+ Tools: openclaw,
                                                              show_qr, stopwatch …)

Push-to-Talk: Das Device steuert die Aufnahmefenster selbst (input.begin/
input.end). Die Bridge nutzt daher KEIN Server-VAD, sondern committed den
Audio-Buffer bei input.end und löst die Antwort aus.

Start:
    pip install -r requirements.txt
    cp .env.example .env   # Werte eintragen (OPENAI_API_KEY …)
    python3 neopuck_openai_bridge.py

Danach im neopuck Captive-Portal als Agent-URL die Bridge eintragen, z.B.
    ws://<rechner-ip>:8765      (Token = NEOPUCK_TOKEN, optional)
"""
import asyncio
import base64
import json
import os
import audio_util as au           # kleiner Resampler (16k <-> 24k), stdlib-only
import websockets

# --------------------------------------------------------------------------- #
# .env neben dem Script laden (ohne Extra-Dependency)
# --------------------------------------------------------------------------- #
def _load_env():
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), ".env")
    if not os.path.exists(path):
        return
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            k, v = line.split("=", 1)
            os.environ.setdefault(k.strip(), v.strip())
_load_env()

# --------------------------------------------------------------------------- #
# Konfiguration (per Umgebungsvariablen / .env)
# --------------------------------------------------------------------------- #
OPENAI_API_KEY = os.environ.get("OPENAI_API_KEY", "")
OPENAI_MODEL   = os.environ.get("OPENAI_MODEL", "gpt-4o-realtime-preview")
OPENAI_VOICE   = os.environ.get("OPENAI_VOICE", "alloy")
BRIDGE_HOST    = os.environ.get("BRIDGE_HOST", "0.0.0.0")
BRIDGE_PORT    = int(os.environ.get("BRIDGE_PORT", "8765"))
NEOPUCK_TOKEN  = os.environ.get("NEOPUCK_TOKEN", "")   # optional: muss zum agent_token passen

# openclaw als Tool (Basis-URL + Basic-Auth). Den genauen Request/Response-Vertrag
# bitte anpassen (siehe ask_openclaw()).
OPENCLAW_URL   = os.environ.get("OPENCLAW_URL", "")
OPENCLAW_USER  = os.environ.get("OPENCLAW_USER", "")
OPENCLAW_PASS  = os.environ.get("OPENCLAW_PASS", "")

DEVICE_SR = 16000     # neopuck PCM16 mono
OPENAI_SR = 24000     # OpenAI Realtime PCM16 mono

SYSTEM_PROMPT = (
    "Du bist neopuck, ein gesprächiger Sprachassistent auf einem kleinen runden "
    "Display. Antworte natürlich, kurz und freundlich auf Deutsch. "
    "Wenn der Nutzer etwas wissen will, das in openclaw steckt, nutze das Tool "
    "ask_openclaw. Du kannst dem Nutzer auch etwas auf dem Display zeigen: "
    "show_qr für einen QR-Code, start_stopwatch für eine Stoppuhr, close_app zum "
    "Schließen. Sag kurz dazu, was du anzeigst."
)

# --------------------------------------------------------------------------- #
# Tool-Definitionen, die das Realtime-Modell aufrufen darf
# --------------------------------------------------------------------------- #
TOOLS = [
    {
        "type": "function",
        "name": "show_qr",
        "description": "Zeigt einen QR-Code auf dem neopuck-Display.",
        "parameters": {
            "type": "object",
            "properties": {
                "data": {"type": "string", "description": "Inhalt/URL für den QR-Code"},
                "caption": {"type": "string", "description": "kurze Bildunterschrift"},
            },
            "required": ["data"],
        },
    },
    {
        "type": "function",
        "name": "start_stopwatch",
        "description": "Startet eine Stoppuhr auf dem neopuck-Display.",
        "parameters": {"type": "object", "properties": {}},
    },
    {
        "type": "function",
        "name": "close_app",
        "description": "Schließt die aktuelle Mini-App auf dem Display.",
        "parameters": {"type": "object", "properties": {}},
    },
    {
        "type": "function",
        "name": "ask_openclaw",
        "description": "Stellt eine Frage an das openclaw-Backend und liefert dessen Antwort.",
        "parameters": {
            "type": "object",
            "properties": {"question": {"type": "string"}},
            "required": ["question"],
        },
    },
]


# --------------------------------------------------------------------------- #
# openclaw-Adapter  (TODO: an euren echten Endpoint anpassen)
# --------------------------------------------------------------------------- #
async def ask_openclaw(question: str) -> str:
    """Fragt openclaw. Aktuell ein bewusst generischer HTTP-POST mit Basic-Auth.
    Den konkreten Pfad + das JSON-Schema bitte an euer Backend anpassen."""
    if not OPENCLAW_URL:
        return "openclaw ist nicht konfiguriert (OPENCLAW_URL fehlt)."
    import aiohttp
    auth = aiohttp.BasicAuth(OPENCLAW_USER, OPENCLAW_PASS) if OPENCLAW_USER else None
    try:
        async with aiohttp.ClientSession() as s:
            async with s.post(OPENCLAW_URL, json={"message": question},
                              auth=auth, timeout=30) as r:
                if r.content_type == "application/json":
                    j = await r.json()
                    # gängige Felder durchprobieren:
                    for k in ("reply", "text", "answer", "content", "message"):
                        if isinstance(j.get(k), str):
                            return j[k]
                    return json.dumps(j)[:1500]
                return (await r.text())[:1500]
    except Exception as e:
        return f"openclaw-Fehler: {e}"


async def run_tool(name: str, args: dict, neopuck) -> str:
    """Führt ein Tool aus. Device-Tools schicken app.launch ans neopuck;
    ask_openclaw geht an euer Backend. Rückgabe = Text fürs Modell."""
    if name == "show_qr":
        await neopuck.send(json.dumps({
            "type": "app.launch", "app": "qr",
            "params": {"data": args.get("data", ""), "caption": args.get("caption", "")},
        }))
        return "QR wird angezeigt."
    if name == "start_stopwatch":
        await neopuck.send(json.dumps({"type": "app.launch", "app": "stopwatch", "params": {}}))
        return "Stoppuhr läuft."
    if name == "close_app":
        await neopuck.send(json.dumps({"type": "app.exit"}))
        return "App geschlossen."
    if name == "ask_openclaw":
        return await ask_openclaw(args.get("question", ""))
    return f"Unbekanntes Tool: {name}"


# --------------------------------------------------------------------------- #
# OpenAI-Realtime: Session konfigurieren
# --------------------------------------------------------------------------- #
async def configure_session(oai):
    await oai.send(json.dumps({
        "type": "session.update",
        "session": {
            "modalities": ["audio", "text"],
            "instructions": SYSTEM_PROMPT,
            "voice": OPENAI_VOICE,
            "input_audio_format": "pcm16",
            "output_audio_format": "pcm16",
            "input_audio_transcription": {"model": "whisper-1"},
            "turn_detection": None,     # Push-to-Talk: Device steuert die Turns
            "tools": TOOLS,
            "tool_choice": "auto",
        },
    }))


# --------------------------------------------------------------------------- #
# Eine Geräteverbindung bedienen
# --------------------------------------------------------------------------- #
async def handle_neopuck(neopuck):
    # Optionales Token prüfen (neopuck schickt es als "Authorization: Bearer …").
    if NEOPUCK_TOKEN:
        hdr = neopuck.request.headers.get("authorization", "")
        if hdr != f"Bearer {NEOPUCK_TOKEN}":
            await neopuck.close(code=4401, reason="bad token")
            return
    print("[neopuck] verbunden")

    oai_url = f"wss://api.openai.com/v1/realtime?model={OPENAI_MODEL}"
    headers = {"Authorization": f"Bearer {OPENAI_API_KEY}", "OpenAI-Beta": "realtime=v1"}

    # websockets >=12 nutzt additional_headers, ältere extra_headers
    try:
        oai = await websockets.connect(oai_url, additional_headers=headers, max_size=None)
    except TypeError:
        oai = await websockets.connect(oai_url, extra_headers=headers, max_size=None)

    async with oai:
        await configure_session(oai)

        async def device_to_openai():
            """neopuck -> OpenAI: Steuer-JSON + Mic-Audio (resampled 16k->24k)."""
            async for msg in neopuck:
                if isinstance(msg, bytes):
                    pcm24 = au.resample(msg, DEVICE_SR, OPENAI_SR)
                    await oai.send(json.dumps({
                        "type": "input_audio_buffer.append",
                        "audio": base64.b64encode(pcm24).decode(),
                    }))
                    continue
                data = json.loads(msg)
                t = data.get("type")
                if t == "session.start":
                    print("[neopuck] session.start", data.get("device"))
                elif t == "input.begin":
                    await oai.send(json.dumps({"type": "input_audio_buffer.clear"}))
                elif t == "input.end":
                    await oai.send(json.dumps({"type": "input_audio_buffer.commit"}))
                    await oai.send(json.dumps({"type": "response.create"}))
                    await neopuck.send(json.dumps({"type": "thinking"}))

        async def openai_to_device():
            """OpenAI -> neopuck: Antwort-Audio (24k->16k) + Transkripte + Tools."""
            speaking = False
            async for raw in oai:
                ev = json.loads(raw)
                et = ev.get("type", "")

                if et == "response.audio.delta":
                    if not speaking:
                        speaking = True
                        await neopuck.send(json.dumps({"type": "response.audio.begin"}))
                    pcm24 = base64.b64decode(ev["delta"])
                    pcm16 = au.resample(pcm24, OPENAI_SR, DEVICE_SR)
                    await neopuck.send(pcm16)            # binär = TTS

                elif et == "response.audio.done":
                    if speaking:
                        speaking = False
                        await neopuck.send(json.dumps({"type": "response.audio.end"}))

                elif et == "response.audio_transcript.delta":
                    await neopuck.send(json.dumps({"type": "response.text",
                                                   "text": ev.get("delta", "")}))

                elif et == "conversation.item.input_audio_transcription.completed":
                    await neopuck.send(json.dumps({"type": "transcript", "role": "user",
                                                   "text": ev.get("transcript", ""),
                                                   "final": True}))

                elif et == "response.function_call_arguments.done":
                    name = ev.get("name", "")
                    try:
                        args = json.loads(ev.get("arguments") or "{}")
                    except json.JSONDecodeError:
                        args = {}
                    print(f"[tool] {name}({args})")
                    result = await run_tool(name, args, neopuck)
                    # Ergebnis zurück ans Modell + neue Antwort anstoßen
                    await oai.send(json.dumps({
                        "type": "conversation.item.create",
                        "item": {"type": "function_call_output",
                                 "call_id": ev.get("call_id"), "output": result},
                    }))
                    await oai.send(json.dumps({"type": "response.create"}))

                elif et == "response.done":
                    await neopuck.send(json.dumps({"type": "response.done"}))

                elif et == "error":
                    print("[openai] error:", ev.get("error"))
                    await neopuck.send(json.dumps({"type": "error",
                                                   "message": str(ev.get("error", {}).get("message", "?"))}))

        try:
            await asyncio.gather(device_to_openai(), openai_to_device())
        except websockets.ConnectionClosed:
            pass
    print("[neopuck] getrennt")


async def main():
    if not OPENAI_API_KEY:
        raise SystemExit("OPENAI_API_KEY fehlt (siehe .env.example)")
    print(f"Bridge läuft auf ws://{BRIDGE_HOST}:{BRIDGE_PORT}  (Modell: {OPENAI_MODEL})")
    async with websockets.serve(handle_neopuck, BRIDGE_HOST, BRIDGE_PORT, max_size=None):
        await asyncio.Future()   # für immer


if __name__ == "__main__":
    asyncio.run(main())

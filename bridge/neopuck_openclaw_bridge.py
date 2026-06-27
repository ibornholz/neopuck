#!/usr/bin/env python3
"""
neopuck ⇄ openclaw Voice-Bridge (Variante 2 — openclaw = Gehirn UND Stimme)
===========================================================================

openclaw kann den kompletten Voice-Loop selbst (STT + Agent + TTS) über seine
`talk.*`-Gateway-API. Diese Bridge ist deshalb nur ein dünner PCM16-Relay:

    neopuck ──ws, PCM16 16k──►  Bridge  ──talk.session.appendAudio──►  openclaw
            ◄──PCM16 16k TTS──          ◄────────── talk.event ────────

KEIN eigener OpenAI-Key nötig. Auth zu openclaw = Ed25519-Geräte-Pairing
(openclaw_client.py). Gerät muss einmal in openclaw freigegeben werden.

Hinweis: Die exakten Feldnamen der `talk.event`-Payloads werden defensiv geparst
und mit OCLAW_DEBUG=1 vollständig geloggt — der erste Live-Lauf bestätigt sie.
"""
import asyncio
import base64
import json
import os
import time

import audio_util as au
import websockets
from openclaw_client import OpenclawClient


def _load_env():
    p = os.path.join(os.path.dirname(os.path.abspath(__file__)), ".env")
    if os.path.exists(p):
        for line in open(p):
            line = line.strip()
            if line and not line.startswith("#") and "=" in line:
                k, v = line.split("=", 1)
                os.environ.setdefault(k.strip(), v.strip())
_load_env()

BRIDGE_HOST   = os.environ.get("BRIDGE_HOST", "0.0.0.0")
BRIDGE_PORT   = int(os.environ.get("BRIDGE_PORT", "8765"))
NEOPUCK_TOKEN = os.environ.get("NEOPUCK_TOKEN", "")
OPENCLAW_WS   = os.environ["OPENCLAW_WS"]
OPENCLAW_USER = os.environ.get("OPENCLAW_USER", "")
OPENCLAW_PASS = os.environ.get("OPENCLAW_PASS", "")
OPENCLAW_TOKEN = os.environ.get("OPENCLAW_GATEWAY_TOKEN", "")
TALK_PROVIDER = os.environ.get("OPENCLAW_TALK_PROVIDER", "microsoft")

DEVICE_SR = 16000
DEBUG = bool(os.environ.get("OCLAW_DEBUG"))


def _find(d, *keys):
    """erstes vorhandenes Feld aus einem dict (case-insensitive flach)."""
    if not isinstance(d, dict):
        return None
    low = {k.lower(): v for k, v in d.items()}
    for k in keys:
        if k.lower() in low and low[k.lower()] is not None:
            return low[k.lower()]
    return None


async def handle_neopuck(neopuck):
    if NEOPUCK_TOKEN:
        if neopuck.request.headers.get("authorization", "") != f"Bearer {NEOPUCK_TOKEN}":
            await neopuck.close(code=4401, reason="bad token"); return
    print("[neopuck] verbunden")

    loop = asyncio.get_event_loop()
    state = {"sid": None, "in_sr": 16000, "out_sr": 24000, "speaking": False}

    # --- openclaw talk.event -> neopuck ---
    def on_event(ev):
        if ev.get("event") != "talk.event":
            return
        payload = ev.get("payload") or {}
        if DEBUG:
            print("[talk.event]", json.dumps(payload)[:400])
        etype = str(_find(payload, "type", "talkEvent", "event") or "")

        # 1) Output-Audio (base64 PCM16) -> Puck
        b64 = _find(payload, "audioBase64", "audio", "delta", "pcm")
        if isinstance(b64, str) and b64 and "audio" in etype.lower() + "delta":
            try:
                pcm = base64.b64decode(b64)
                pcm16 = au.resample(pcm, state["out_sr"], DEVICE_SR)
                if not state["speaking"]:
                    state["speaking"] = True
                    loop.create_task(neopuck.send(json.dumps({"type": "response.audio.begin"})))
                loop.create_task(neopuck.send(pcm16))
            except Exception as e:
                print("[audio out err]", e)
            return

        # 2) Transkripte / Antworttext
        txt = _find(payload, "transcript", "text")
        role = _find(payload, "role") or "assistant"
        if isinstance(txt, str) and txt:
            t = "transcript" if role == "user" else "response.text"
            msg = {"type": t, "text": txt}
            if t == "transcript":
                msg.update({"role": "user", "final": bool(_find(payload, "final"))})
            loop.create_task(neopuck.send(json.dumps(msg)))

        # 3) Turn-Lebenszyklus
        if "thinking" in etype or etype.endswith("turn.started"):
            loop.create_task(neopuck.send(json.dumps({"type": "thinking"})))
        if etype.endswith("turn.ended") or "done" in etype or etype.endswith("close"):
            if state["speaking"]:
                state["speaking"] = False
                loop.create_task(neopuck.send(json.dumps({"type": "response.audio.end"})))
            loop.create_task(neopuck.send(json.dumps({"type": "response.done"})))

        # 4) Tool-Calls vom Agent -> Mini-Apps am Device
        if "tool" in etype:
            name = _find(payload, "name", "tool", "toolName")
            args = _find(payload, "arguments", "args", "params") or {}
            if isinstance(args, str):
                try: args = json.loads(args)
                except json.JSONDecodeError: args = {}
            if name == "show_qr":
                loop.create_task(neopuck.send(json.dumps({"type": "app.launch", "app": "qr",
                    "params": {"data": args.get("data", ""), "caption": args.get("caption", "")}})))
            elif name in ("start_stopwatch", "stopwatch"):
                loop.create_task(neopuck.send(json.dumps({"type": "app.launch", "app": "stopwatch", "params": {}})))
            elif name in ("close_app", "exit"):
                loop.create_task(neopuck.send(json.dumps({"type": "app.exit"})))

    oc = OpenclawClient(OPENCLAW_WS, OPENCLAW_USER, OPENCLAW_PASS,
                        gateway_token=OPENCLAW_TOKEN, on_event=on_event)
    oc_task = asyncio.create_task(oc.run())
    try:
        await asyncio.wait_for(oc.connected.wait(), timeout=20)
    except asyncio.TimeoutError:
        print("[openclaw] connect timeout (Gerät freigegeben? Gateway erreichbar?)")
        oc_task.cancel(); await neopuck.close(); return
    print("[openclaw] verbunden")

    # talk-Session anlegen (server-seitige Stimme: stt-tts / gateway-relay / agent-consult)
    create = await oc.request("talk.session.create", {
        "sessionKey": "neopuck-bridge", "mode": "stt-tts",
        "transport": "gateway-relay", "brain": "agent-consult",
        "provider": TALK_PROVIDER,
    })
    if DEBUG:
        print("[talk.session.create]", json.dumps(create)[:800])
    sess = create.get("session", create)
    state["sid"] = _find(create, "relaySessionId", "sessionId", "id") or _find(sess, "relaySessionId", "id")
    audio = _find(sess, "audio") or {}
    state["in_sr"]  = int(_find(audio, "inputSampleRateHz") or 16000)
    state["out_sr"] = int(_find(audio, "outputSampleRateHz") or 24000)
    print(f"[openclaw] talk session {state['sid']} (in {state['in_sr']}Hz / out {state['out_sr']}Hz)")

    # --- neopuck -> openclaw ---
    try:
        async for msg in neopuck:
            if isinstance(msg, bytes):
                if not state["sid"]:
                    continue
                pcm = au.resample(msg, DEVICE_SR, state["in_sr"])
                await oc.request("talk.session.appendAudio", {
                    "sessionId": state["sid"],
                    "audioBase64": base64.b64encode(pcm).decode(),
                    "timestamp": int(time.time() * 1000),
                })
                continue
            data = json.loads(msg)
            t = data.get("type")
            if t == "input.begin" and DEBUG:
                print("[neopuck] input.begin")
            elif t == "input.end" and DEBUG:
                print("[neopuck] input.end")
    except websockets.ConnectionClosed:
        pass
    finally:
        if state["sid"]:
            try: await oc.request("talk.session.close", {"sessionId": state["sid"]})
            except Exception: pass
        oc_task.cancel()
        print("[neopuck] getrennt")


async def main():
    print(f"openclaw-Bridge läuft auf ws://{BRIDGE_HOST}:{BRIDGE_PORT}")
    print(f"openclaw: {OPENCLAW_WS}  (Provider: {TALK_PROVIDER})")
    async with websockets.serve(handle_neopuck, BRIDGE_HOST, BRIDGE_PORT, max_size=None):
        await asyncio.Future()


if __name__ == "__main__":
    asyncio.run(main())

"""openclaw-Gateway-Client (Geräte-Pairing, nachgebaut aus dem Kite-Web-Client).

Auth-Modell von openclaw: Ed25519-Geräteidentität.
- deviceId = hex(SHA-256(publicKey))
- Auf die `connect.challenge` (Nonce) antwortet der Client mit
  {type:"req",method:"connect",params:{… device:{id,publicKey,signature,signedAt,nonce}}}
  signature = Ed25519(privateKey, "v2|deviceId|clientId|clientMode|role|scopes|signedAt|token|nonce")
- Neue Geräte sind erst "not-paired" und müssen in openclaw freigegeben werden.

Identität wird lokal in .openclaw_identity.json gehalten (gitignored), damit die
deviceId stabil bleibt (einmal freigeben reicht).
"""
import asyncio
import base64
import hashlib
import json
import os
import time
import uuid

import websockets
from nacl import signing

IDENTITY_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), ".openclaw_identity.json")

CLIENT_ID   = "webchat"
CLIENT_MODE = "webchat"
ROLE        = "operator"
SCOPES      = ["operator.admin", "operator.read", "operator.write",
               "operator.approvals", "operator.pairing"]


def _b64url(b: bytes) -> str:
    return base64.urlsafe_b64encode(b).decode().rstrip("=")


def load_or_create_identity() -> dict:
    if os.path.exists(IDENTITY_FILE):
        with open(IDENTITY_FILE) as f:
            return json.load(f)
    sk = signing.SigningKey.generate()
    pub = bytes(sk.verify_key)
    ident = {
        "deviceId": hashlib.sha256(pub).hexdigest(),
        "publicKey": _b64url(pub),
        "privateKey": _b64url(bytes(sk)),   # 32-byte seed
    }
    with open(IDENTITY_FILE, "w") as f:
        json.dump(ident, f, indent=2)
    return ident


def _sign(priv_b64url: str, msg: str) -> str:
    seed = base64.urlsafe_b64decode(priv_b64url + "=" * ((4 - len(priv_b64url) % 4) % 4))
    sk = signing.SigningKey(seed)
    return _b64url(sk.sign(msg.encode()).signature)


class OpenclawClient:
    """Minimaler Gateway-Client: connect-Handshake + request/response + Events."""

    def __init__(self, url: str, basic_user: str, basic_pass: str,
                 gateway_token: str = "", on_event=None):
        self.url = url
        self.basic = base64.b64encode(f"{basic_user}:{basic_pass}".encode()).decode()
        self.gateway_token = gateway_token or ""
        self.ident = load_or_create_identity()
        self.ws = None
        self._pending = {}          # req-id -> Future
        self._on_event = on_event   # callback(event_dict)
        self.connected = asyncio.Event()
        self._connect_started = False

    def _connect_params(self, nonce: str) -> dict:
        signed_at = int(time.time() * 1000)
        # Der Gateway-Token geht in die signierte Canonical-Zeile UND in auth.token.
        canonical = "|".join([
            "v2", self.ident["deviceId"], CLIENT_ID, CLIENT_MODE, ROLE,
            ",".join(SCOPES), str(signed_at), self.gateway_token, nonce,
        ])
        sig = _sign(self.ident["privateKey"], canonical)
        return {
            "minProtocol": 4, "maxProtocol": 4,
            "client": {"id": CLIENT_ID, "version": "neopuck-bridge",
                       "platform": "server", "mode": CLIENT_MODE,
                       "instanceId": str(uuid.uuid4())},
            "role": ROLE, "scopes": SCOPES,
            "device": {"id": self.ident["deviceId"], "publicKey": self.ident["publicKey"],
                       "signature": sig, "signedAt": signed_at, "nonce": nonce},
            "caps": ["tool-events"],
            "auth": {"token": self.gateway_token} if self.gateway_token else {},
            "userAgent": "neopuck-bridge",
        }

    async def _send(self, obj):
        await self.ws.send(json.dumps(obj))

    async def request(self, method: str, params: dict):
        rid = str(uuid.uuid4())
        fut = asyncio.get_event_loop().create_future()
        self._pending[rid] = fut
        await self._send({"type": "req", "id": rid, "method": method, "params": params})
        return await asyncio.wait_for(fut, timeout=30)

    async def run(self):
        # Origin muss zum Gateway-Host passen (CONTROL_UI_ORIGIN_NOT_ALLOWED sonst).
        from urllib.parse import urlsplit
        u = urlsplit(self.url)
        origin = f"https://{u.netloc}"
        headers = {"Authorization": "Basic " + self.basic, "Origin": origin}
        try:
            self.ws = await websockets.connect(self.url, additional_headers=headers, max_size=None)
        except TypeError:
            self.ws = await websockets.connect(self.url, extra_headers=headers, max_size=None)

        # Fallback: kommt keine connect.challenge, Connect trotzdem senden (Nonce="").
        asyncio.create_task(self._fallback_connect())

        async for raw in self.ws:
            if os.environ.get("OCLAW_DEBUG"):
                print("[raw]", str(raw)[:700])
            try:
                msg = json.loads(raw)
            except json.JSONDecodeError:
                continue
            t = msg.get("type")
            if t == "event" and msg.get("event") == "connect.challenge":
                if not self._connect_started:
                    self._connect_started = True
                    nonce = msg["payload"]["nonce"]
                    asyncio.create_task(self._do_connect(nonce))
            elif t in ("res", "response"):
                fut = self._pending.pop(msg.get("id"), None)
                if fut and not fut.done():
                    if msg.get("ok") is False or msg.get("error"):
                        fut.set_exception(RuntimeError(json.dumps(msg.get("error") or msg)))
                    else:
                        fut.set_result(msg.get("result", msg.get("payload", msg)))
            elif t == "event":
                if msg.get("event") == "connect.hello" or msg.get("event") == "ready":
                    self.connected.set()
                if self._on_event:
                    self._on_event(msg)

    async def _fallback_connect(self):
        await asyncio.sleep(1.5)
        if not self._connect_started and self.ws is not None:
            self._connect_started = True
            await self._do_connect("")   # ohne Challenge -> Nonce ""

    async def _do_connect(self, nonce: str):
        rid = str(uuid.uuid4())
        fut = asyncio.get_event_loop().create_future()
        self._pending[rid] = fut
        await self._send({"type": "req", "id": rid, "method": "connect",
                          "params": self._connect_params(nonce)})
        try:
            res = await asyncio.wait_for(fut, timeout=30)
            print("[openclaw] connect.hello:", json.dumps(res)[:600])
            self.connected.set()
        except Exception as e:
            print("[openclaw] connect FAILED:", e)

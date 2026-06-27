# Bridge auf dem Server deployen + Updates

Die Bridge ist ein kleiner Python-WebSocket-Dienst. Sinnvollster Ort: **derselbe
Server wie openclaw (`new.bornholz.com`)** — dann ist openclaw lokal erreichbar,
TLS kommt vom vorhandenen Apache, und der Puck erreicht die Bridge rund um die Uhr.

## Was die Bridge erreichbar braucht
- **ausgehend:** openclaw-Gateway (lokal/`wss`) und ggf. `api.openai.com`
- **eingehend:** der neopuck verbindet sich auf Port **8765** (→ hinter TLS als `wss://`)

## Persistente Dateien (WICHTIG für Updates)
Zwei Dateien dürfen bei Updates **nicht** verloren gehen / überschrieben werden:
- `.env` — die Konfiguration/Secrets
- `.openclaw_identity.json` — die **Ed25519-Geräteidentität**. Bleibt sie erhalten,
  muss das Gerät **nur einmal** in openclaw freigegeben werden. Geht sie verloren,
  entsteht eine neue `deviceId` → erneute Pairing-Freigabe nötig.

Beide liegen außerhalb des Images bzw. werden als Volume gemountet (siehe unten).

---

## Variante A — Docker (empfohlen)
```bash
cp .env.example .env            # ausfüllen
touch .openclaw_identity.json   # wird beim ersten Start befüllt
docker compose up -d --build
docker compose logs -f          # deviceId ablesen -> in openclaw freigeben
```

## Variante B — systemd
```bash
sudo mkdir -p /opt/neopuck-bridge && sudo cp -r . /opt/neopuck-bridge
cd /opt/neopuck-bridge
python3 -m venv .venv && ./.venv/bin/pip install -r requirements.txt
cp .env.example .env            # ausfüllen
sudo cp neopuck-bridge.service /etc/systemd/system/
sudo systemctl enable --now neopuck-bridge
journalctl -u neopuck-bridge -f # deviceId ablesen -> in openclaw freigeben
```

## TLS / Reverse-Proxy (Apache, schon vorhanden)
Damit der Puck `wss://` nutzen kann, eine WebSocket-Weiterleitung anlegen, z.B.:
```apache
# in der vhost-Konfig von new.bornholz.com
RewriteEngine On
RewriteCond %{HTTP:Upgrade} =websocket [NC]
RewriteRule /neopuck-bridge/?(.*) ws://127.0.0.1:8765/$1 [P,L]
ProxyPass        /neopuck-bridge  ws://127.0.0.1:8765/
ProxyPassReverse /neopuck-bridge  ws://127.0.0.1:8765/
```
Im neopuck-Portal dann als Agent-URL: `wss://new.bornholz.com/neopuck-bridge`.

## Einmalig: Gerät freigeben
Beim ersten Start loggt die Bridge ihre `deviceId`. Diese in openclaw unter den
**pending pairing requests** als **operator** freigeben. Danach verbindet sich die
Bridge automatisch.

---

## Updates in Zukunft
Die Bridge liegt im git-Repo (`bridge/`). Update-Ablauf:

**Docker:**
```bash
git pull
docker compose up -d --build      # .env + .openclaw_identity.json bleiben (Volume)
```
**systemd:**
```bash
git pull        # bzw. neue Dateien nach /opt/neopuck-bridge kopieren (NICHT .env/identity)
./.venv/bin/pip install -r requirements.txt
sudo systemctl restart neopuck-bridge
```

Empfehlungen:
- **Versionieren** (git tags), damit Rollbacks einfach sind.
- Das openclaw-Protokoll ist in **`openclaw_client.py` isoliert**. Ändert openclaw
  den Handshake, muss nur diese Datei angepasst werden — der Rest bleibt stabil.
- Langfristig robuster wäre ein **stabiler Server-Endpoint** in openclaw für die
  Bridge (statt des Browser-Geräteprotokolls). Dann entfällt die Pairing-/Krypto-
  Logik komplett.

# iPhone-Anbindung — Provisioning & Management

## Sofort nutzbar (keine eigene App nötig)

1. App Store: **"ESP BLE Provisioning"** (Espressif) installieren.
2. Device einschalten → es zeigt den Pairing-Screen (`PROV_neopuck-01`, PIN `neopuck-pop`).
3. In der App das Gerät auswählen, PIN eingeben.
4. WLAN wählen — **hier den Persönlichen Hotspot deines iPhones eintragen**,
   wenn das Device übers iPhone ins Netz soll (Hotspot vorher aktivieren).
5. Custom-Data-Feld (`agent-config`) mit dem Agent-JSON füllen:

```json
{
  "agent_url":   "wss://agent.neonet.ai/voice",
  "agent_token": "…",
  "proto":       "neonet",
  "device_name": "neopuck-01",
  "push_to_talk": true,
  "volume": 70,
  "brightness": 80
}
```

Die Firmware nimmt WLAN-Creds über den Standardpfad, Agent-Settings über den
Custom-Endpoint `agent-config` und meldet Live-Status über `device-status`.

## Eigene App (späteres, separates Deliverable)

Eine schlanke SwiftUI-App spricht denselben Provisioning-Stack über CoreBluetooth.
Espressif liefert dafür das **ESPProvision** Swift-Package — Grundgerüst:

```swift
import ESPProvision

// 1) Scannen & verbinden
ESPProvisionManager.shared.searchESPDevices(
    devicePrefix: "PROV_", transport: .ble, security: .secure) { devices, _ in
    guard let dev = devices?.first else { return }
    dev.connect(delegate: self) { status in
        // 2) WLAN setzen (Hotspot-SSID/-Pass)
        dev.provision(ssid: ssid, passPhrase: pass) { state in /* … */ }

        // 3) Agent-Config über Custom-Endpoint pushen
        let json = try! JSONSerialization.data(withJSONObject: [
            "agent_url": agentURL, "agent_token": token, "proto": "neonet"
        ])
        dev.sendData(path: "agent-config", data: json) { _, _ in }

        // 4) Status lesen
        dev.sendData(path: "device-status", data: Data()) { resp, _ in
            // resp = {"state":..,"agent":..,"provisioned":..}
        }
    }
}
```

Das ist genau die Schnittstelle, die `provisioning.c` (Endpoints `agent-config`,
`device-status`) bereitstellt — App und Firmware passen also zusammen, sobald du
die App bauen willst.

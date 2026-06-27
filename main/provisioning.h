// provisioning.h — BLE-Provisioning fürs iPhone (WLAN + Agent-Config + Status).
#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Startet WiFi-Station + (falls nötig) BLE-Provisioning.
// Bei bereits konfiguriertem Gerät: direkt mit gespeicherten Creds verbinden.
// Setzt EV_WIFI_UP / EV_WIFI_DOWN / EV_PROV_DONE in g_events.
void provisioning_start(void);

// Provisioning erneut öffnen (Settings-Screen / BOOT lang).
void provisioning_reopen(void);

// liefert den BLE-Service-Namen für den QR-/Pairing-Screen (z.B. "PROV_neopuck-01")
const char *provisioning_service_name(void);

#ifdef __cplusplus
}
#endif

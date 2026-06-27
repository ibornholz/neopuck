// provisioning.h — App-freies Provisioning via SoftAP + Captive Portal (Task 3).
#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Startet WiFi. Bereits konfiguriert -> direkt mit gespeicherten Creds verbinden,
// sonst SoftAP "neopuck-setup" + Captive Portal öffnen.
// Setzt EV_WIFI_UP / EV_WIFI_DOWN / EV_PROV_DONE in g_events.
void provisioning_start(void);

// Provisioning-Portal erneut öffnen (Settings-Screen / BOOT lang).
void provisioning_reopen(void);

// liefert die Setup-WLAN-SSID für den QR-/Portal-Screen (z.B. "neopuck-setup").
const char *provisioning_service_name(void);

#ifdef __cplusplus
}
#endif

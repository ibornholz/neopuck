// bsp_internal.h — komponenteninterne, gemeinsam genutzte Handles.
#pragma once
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialisiert den gemeinsamen I2C-Master-Bus (idempotent). Touch, Audio-Codecs
// und der AXP2101 hängen alle an diesem Bus.
esp_err_t bsp_i2c_init(void);
i2c_master_bus_handle_t bsp_i2c_get_handle(void);

// --- AXP2101 PMU (bsp_power.cpp) ---------------------------------------------
esp_err_t bsp_power_init(void);   // PMU am gemeinsamen I2C-Bus hochfahren

// Power-Key (PWR-Taste) abfragen. Da die PWR-Taste am AXP2101-PEKEY hängt und
// nicht an einem GPIO, pollt der Button-Treiber den IRQ-Status des PMU.
typedef enum {
    BSP_PEKEY_NONE = 0,
    BSP_PEKEY_SHORT,
    BSP_PEKEY_LONG,
} bsp_pekey_evt_t;
bsp_pekey_evt_t bsp_power_poll_pekey(void);

#ifdef __cplusplus
}
#endif

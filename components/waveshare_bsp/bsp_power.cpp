// bsp_power.cpp — AXP2101 PMU über XPowersLib (C++), C-Schnittstelle nach board.h.
// Akku/Ladezustand + Power-Key (PWR-Taste) hängen am AXP2101 auf dem I2C-Bus.
#include "board.h"
#include "bsp_internal.h"
#include "bsp_pins.h"

#include "esp_log.h"
#include "driver/i2c_master.h"

#define XPOWERS_CHIP_AXP2101
#include "XPowersLib.h"

static const char *TAG = "bsp_pmu";
static XPowersPMU s_pmu;
static i2c_master_dev_handle_t s_dev;
static bool s_ready;

// --- I2C-Callbacks für XPowersLib (neue i2c_master-API) ----------------------
static int pmu_read(uint8_t dev, uint8_t reg, uint8_t *data, uint8_t len)
{
    (void)dev;
    return i2c_master_transmit_receive(s_dev, &reg, 1, data, len, 1000) == ESP_OK ? 0 : -1;
}
static int pmu_write(uint8_t dev, uint8_t reg, uint8_t *data, uint8_t len)
{
    (void)dev;
    uint8_t buf[16];
    if (len + 1 > (int)sizeof(buf)) return -1;
    buf[0] = reg;
    for (uint8_t i = 0; i < len; i++) buf[i + 1] = data[i];
    return i2c_master_transmit(s_dev, buf, len + 1, 1000) == ESP_OK ? 0 : -1;
}

extern "C" esp_err_t bsp_power_init(void)
{
    if (s_ready) return ESP_OK;
    if (bsp_i2c_init() != ESP_OK) return ESP_FAIL;

    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address  = BSP_AXP2101_I2C_ADDR;
    dev_cfg.scl_speed_hz    = BSP_I2C_CLK_HZ;
    if (i2c_master_bus_add_device(bsp_i2c_get_handle(), &dev_cfg, &s_dev) != ESP_OK) {
        ESP_LOGE(TAG, "i2c add AXP2101 failed");
        return ESP_FAIL;
    }

    if (!s_pmu.begin(BSP_AXP2101_I2C_ADDR, pmu_read, pmu_write)) {
        ESP_LOGE(TAG, "AXP2101 begin failed");
        return ESP_FAIL;
    }

    // Messpfade aktivieren (Akku-%/Spannung), TS-Pin aus (kein Batt-Temp-Sensor).
    s_pmu.enableBattVoltageMeasure();
    s_pmu.enableVbusVoltageMeasure();
    s_pmu.enableSystemVoltageMeasure();
    s_pmu.disableTSPinMeasure();

    // Laderegime wie im offiziellen Beispiel.
    s_pmu.setPrechargeCurr(XPOWERS_AXP2101_PRECHARGE_50MA);
    s_pmu.setChargerConstantCurr(XPOWERS_AXP2101_CHG_CUR_400MA);
    s_pmu.setChargerTerminationCurr(XPOWERS_AXP2101_CHG_ITERM_25MA);
    s_pmu.setChargeTargetVoltage(XPOWERS_AXP2101_CHG_VOL_4V2);

    // Nur Power-Key-IRQs für die PWR-Taste.
    s_pmu.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
    s_pmu.clearIrqStatus();
    s_pmu.enableIRQ(XPOWERS_AXP2101_PKEY_SHORT_IRQ | XPOWERS_AXP2101_PKEY_LONG_IRQ);

    s_ready = true;
    ESP_LOGI(TAG, "AXP2101 up, battery=%d%%", s_pmu.getBatteryPercent());
    return ESP_OK;
}

extern "C" uint8_t bsp_battery_percent(void)
{
    if (!s_ready && bsp_power_init() != ESP_OK) return 0;
    int p = s_pmu.getBatteryPercent();
    if (p < 0) return s_pmu.isVbusIn() ? 100 : 0;   // keine Batterie -> USB-Versorgung
    if (p > 100) p = 100;
    return (uint8_t)p;
}

extern "C" bool bsp_is_charging(void)
{
    if (!s_ready && bsp_power_init() != ESP_OK) return false;
    return s_pmu.isCharging();
}

extern "C" bsp_pekey_evt_t bsp_power_poll_pekey(void)
{
    if (!s_ready) return BSP_PEKEY_NONE;
    s_pmu.getIrqStatus();
    bsp_pekey_evt_t ev = BSP_PEKEY_NONE;
    if (s_pmu.isPekeyLongPressIrq())  ev = BSP_PEKEY_LONG;
    else if (s_pmu.isPekeyShortPressIrq()) ev = BSP_PEKEY_SHORT;
    s_pmu.clearIrqStatus();
    return ev;
}

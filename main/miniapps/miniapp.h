// miniapp.h — Mini-App-Runtime (Task 4). Der Agent startet per app.launch eine
// kleine On-Device-App; app.exit oder ein Exit-Touch beenden sie wieder.
#pragma once
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Eine Mini-App. launch() bekommt einen leeren, bildschirmfüllenden Container.
typedef struct {
    const char *id;
    void (*launch)(lv_obj_t *parent, const char *params_json);
    void (*tick)(uint32_t dt_ms);
    void (*on_touch)(lv_point_t p);
    void (*exit)(void);
} miniapp_t;

// Runtime-Steuerung (aus der State-Machine aufgerufen).
void miniapp_start(const char *id, const char *params_json);  // -> ST_MINIAPP
void miniapp_stop(void);                                      // -> ST_IDLE
bool miniapp_active(void);

#ifdef __cplusplus
}
#endif

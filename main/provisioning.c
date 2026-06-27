// provisioning.c — App-freies Provisioning: SoftAP + Captive Portal (Task 3).
// Kein BLE, keine App: Handy verbindet sich mit dem offenen WLAN "neopuck-setup"
// (QR auf dem AMOLED), das Captive-Portal-Popup öffnet die Config-Seite. Felder
// werden als JSON an config_apply_json() gegeben; danach verbindet sich das Gerät
// selbst mit dem Heim-WLAN.
#include "provisioning.h"
#include "app_state.h"
#include "config_store.h"
#include "ui/ui.h"

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "esp_http_server.h"
#include "cJSON.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

static const char *TAG = "prov";

#define SETUP_SSID   "neopuck-setup"
#define PORTAL_IP    "192.168.4.1"

static char            s_service_name[32] = SETUP_SSID;
static httpd_handle_t  s_http;
static TaskHandle_t    s_dns_task;
static volatile bool   s_dns_run;
static bool            s_inited;
static bool            s_portal_running;

const char *provisioning_service_name(void) { return s_service_name; }

// ============================ Captive-DNS ====================================
// Beantwortet JEDE A-Anfrage mit 192.168.4.1, damit das Portal-Popup erscheint.
static void dns_task(void *arg)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) { ESP_LOGE(TAG, "dns socket"); vTaskDelete(NULL); return; }

    struct sockaddr_in sa = {
        .sin_family = AF_INET,
        .sin_port = htons(53),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(sock, (struct sockaddr *)&sa, sizeof sa) < 0) {
        ESP_LOGE(TAG, "dns bind"); close(sock); vTaskDelete(NULL); return;
    }
    struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);

    uint8_t buf[512];
    while (s_dns_run) {
        struct sockaddr_in src; socklen_t sl = sizeof src;
        int n = recvfrom(sock, buf, sizeof buf, 0, (struct sockaddr *)&src, &sl);
        if (n < (int)sizeof(uint16_t) * 6) continue;

        // Minimal-Antwort: Frage spiegeln, eine A-Antwort auf PORTAL_IP anhängen.
        buf[2] |= 0x80;            // QR=1 (Response)
        buf[3] = 0x00;            // keine Fehler, nicht autoritativ
        buf[6] = 0x00; buf[7] = 0x01;  // ANCOUNT = 1 (eine Answer)
        buf[8] = buf[9] = buf[10] = buf[11] = 0; // NSCOUNT/ARCOUNT = 0

        int len = n;
        uint8_t ans[] = {
            0xC0, 0x0C,             // Pointer auf den Fragenamen (Offset 12)
            0x00, 0x01,             // TYPE A
            0x00, 0x01,             // CLASS IN
            0x00, 0x00, 0x00, 0x3C, // TTL 60s
            0x00, 0x04,             // RDLENGTH 4
            192, 168, 4, 1,         // PORTAL_IP
        };
        if (len + (int)sizeof ans <= (int)sizeof buf) {
            memcpy(buf + len, ans, sizeof ans);
            len += sizeof ans;
            sendto(sock, buf, len, 0, (struct sockaddr *)&src, sl);
        }
    }
    close(sock);
    s_dns_task = NULL;
    vTaskDelete(NULL);
}

// ============================ HTTP-Portal ====================================
static const char PAGE_HTML[] =
"<!doctype html><html lang=de><head><meta charset=utf-8>"
"<meta name=viewport content='width=device-width,initial-scale=1'>"
"<title>neopuck Setup</title><style>"
"body{margin:0;background:#000;color:#F2F2F7;font-family:-apple-system,system-ui,sans-serif;}"
".w{max-width:420px;margin:0 auto;padding:28px 22px;}"
"h1{font-size:22px;font-weight:700;}h1 span{color:#7C3AED;}"
"p{color:#8A8A8E;font-size:14px;}"
"label{display:block;margin:16px 0 6px;font-size:13px;color:#C7C7CC;}"
"input,select{width:100%;box-sizing:border-box;padding:12px;border-radius:12px;"
"border:1px solid #3B1F73;background:#0c0c0e;color:#fff;font-size:16px;}"
"button{margin-top:24px;width:100%;padding:14px;border:0;border-radius:12px;"
"background:#7C3AED;color:#fff;font-size:16px;font-weight:600;}"
".r{margin-top:10px;font-size:12px;color:#8A8A8E;}"
"</style></head><body><div class=w>"
"<h1>neo<span>puck</span> Setup</h1>"
"<p>WLAN und Agent verbinden.</p>"
"<form method=POST action=/save>"
"<label>WLAN-Name</label>"
"<input list=nets name=wifi_ssid placeholder='SSID' required>"
"<datalist id=nets></datalist>"
"<label>WLAN-Passwort</label>"
"<input name=wifi_pass type=password placeholder='Passwort'>"
"<label>Agent-URL</label>"
"<input name=agent_url type=url placeholder='wss://host/path' required>"
"<label>Agent-Token</label>"
"<input name=agent_token placeholder='optional'>"
"<button type=submit>Speichern &amp; verbinden</button>"
"<div class=r id=r></div></form></div>"
"<script>"
"fetch('/scan').then(r=>r.json()).then(j=>{let d=document.getElementById('nets');"
"j.forEach(n=>{let o=document.createElement('option');o.value=n.ssid;"
"o.label=n.ssid+' ('+n.rssi+'dBm'+(n.lock?' \\uD83D\\uDD12':'')+')';d.appendChild(o);});})"
".catch(e=>{});"
"</script></body></html>";

static esp_err_t h_root(httpd_req_t *r)
{
    httpd_resp_set_type(r, "text/html");
    return httpd_resp_send(r, PAGE_HTML, HTTPD_RESP_USE_STRLEN);
}

// Captive-Portal-Erkennung: alle Probe-URLs auf das Portal umleiten.
static esp_err_t h_redirect(httpd_req_t *r)
{
    httpd_resp_set_status(r, "302 Found");
    httpd_resp_set_hdr(r, "Location", "http://" PORTAL_IP "/");
    return httpd_resp_send(r, NULL, 0);
}

static esp_err_t h_scan(httpd_req_t *r)
{
    wifi_scan_config_t sc = { .show_hidden = false };
    esp_wifi_scan_start(&sc, true);
    uint16_t num = 0;
    esp_wifi_scan_get_ap_num(&num);
    if (num > 20) num = 20;
    wifi_ap_record_t *recs = calloc(num, sizeof(wifi_ap_record_t));
    cJSON *arr = cJSON_CreateArray();
    if (recs && num) {
        esp_wifi_scan_get_ap_records(&num, recs);
        for (int i = 0; i < num; i++) {
            if (recs[i].ssid[0] == '\0') continue;
            cJSON *o = cJSON_CreateObject();
            cJSON_AddStringToObject(o, "ssid", (char *)recs[i].ssid);
            cJSON_AddNumberToObject(o, "rssi", recs[i].rssi);
            cJSON_AddBoolToObject(o, "lock", recs[i].authmode != WIFI_AUTH_OPEN);
            cJSON_AddItemToArray(arr, o);
        }
    }
    free(recs);
    char *js = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    httpd_resp_set_type(r, "application/json");
    httpd_resp_send(r, js ? js : "[]", HTTPD_RESP_USE_STRLEN);
    free(js);
    return ESP_OK;
}

// einen Wert aus dem urlencoded POST-Body holen + URL-decoden
static void form_get(const char *body, const char *key, char *out, size_t out_sz)
{
    out[0] = '\0';
    size_t klen = strlen(key);
    const char *p = body;
    while ((p = strstr(p, key)) != NULL) {
        if ((p == body || p[-1] == '&') && p[klen] == '=') {
            p += klen + 1;
            size_t i = 0;
            while (*p && *p != '&' && i < out_sz - 1) {
                if (*p == '+') { out[i++] = ' '; p++; }
                else if (*p == '%' && p[1] && p[2]) {
                    char h[3] = { p[1], p[2], 0 };
                    out[i++] = (char)strtol(h, NULL, 16);
                    p += 3;
                } else out[i++] = *p++;
            }
            out[i] = '\0';
            return;
        }
        p += klen;
    }
}

static esp_err_t h_save(httpd_req_t *r)
{
    int total = r->content_len;
    if (total <= 0 || total > 1024) { httpd_resp_send_500(r); return ESP_FAIL; }
    char *body = malloc(total + 1);
    if (!body) { httpd_resp_send_500(r); return ESP_FAIL; }
    int got = 0;
    while (got < total) {
        int n = httpd_req_recv(r, body + got, total - got);
        if (n <= 0) { free(body); httpd_resp_send_500(r); return ESP_FAIL; }
        got += n;
    }
    body[total] = '\0';

    char ssid[33] = {0}, pass[65] = {0}, url[160] = {0}, token[129] = {0};
    form_get(body, "wifi_ssid",   ssid,  sizeof ssid);
    form_get(body, "wifi_pass",   pass,  sizeof pass);
    form_get(body, "agent_url",   url,   sizeof url);
    form_get(body, "agent_token", token, sizeof token);
    free(body);

    // Felder als JSON in den bestehenden Config-Pfad (keine Creds in der URL).
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "wifi_ssid", ssid);
    cJSON_AddStringToObject(j, "wifi_pass", pass);
    cJSON_AddStringToObject(j, "agent_url", url);
    cJSON_AddStringToObject(j, "agent_token", token);
    char *js = cJSON_PrintUnformatted(j);
    cJSON_Delete(j);
    if (js) { config_apply_json(js, strlen(js)); free(js); }

    httpd_resp_set_type(r, "text/html");
    httpd_resp_send(r,
        "<html><body style='background:#000;color:#fff;font-family:sans-serif;"
        "text-align:center;padding-top:80px'><h2>Gespeichert.</h2>"
        "<p style='color:#8A8A8E'>neopuck verbindet sich jetzt…</p></body></html>",
        HTTPD_RESP_USE_STRLEN);

    ESP_LOGI(TAG, "creds gespeichert, wechsle in den Verbindungsmodus");
    xEventGroupSetBits(g_events, EV_PROV_DONE);   // app_loop stoppt Portal + verbindet
    return ESP_OK;
}

static void http_start(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 8;
    cfg.uri_match_fn = httpd_uri_match_wildcard;
    if (httpd_start(&s_http, &cfg) != ESP_OK) { ESP_LOGE(TAG, "httpd start"); return; }

    const httpd_uri_t root = { .uri = "/",       .method = HTTP_GET,  .handler = h_root };
    const httpd_uri_t scan = { .uri = "/scan",   .method = HTTP_GET,  .handler = h_scan };
    const httpd_uri_t save = { .uri = "/save",   .method = HTTP_POST, .handler = h_save };
    const httpd_uri_t any  = { .uri = "/*",      .method = HTTP_GET,  .handler = h_redirect };
    httpd_register_uri_handler(s_http, &root);
    httpd_register_uri_handler(s_http, &scan);
    httpd_register_uri_handler(s_http, &save);
    httpd_register_uri_handler(s_http, &any);   // Captive-Redirect (Fallback)
}

static void http_stop(void)
{
    if (s_http) { httpd_stop(s_http); s_http = NULL; }
}

// ============================ WiFi-Events ====================================
static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupSetBits(g_events, EV_WIFI_DOWN);
        if (!s_portal_running) esp_wifi_connect();   // simpler Auto-Reconnect
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "got IP -> wifi up");
        xEventGroupSetBits(g_events, EV_WIFI_UP);
    }
}

// ============================ Portal / STA ===================================
static void start_portal(void)
{
    s_portal_running = true;
    strncpy(s_service_name, SETUP_SSID, sizeof s_service_name - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));   // AP fürs Portal, STA fürs Scannen
    wifi_config_t ap = {0};
    strncpy((char *)ap.ap.ssid, SETUP_SSID, sizeof ap.ap.ssid - 1);
    ap.ap.ssid_len   = strlen(SETUP_SSID);
    ap.ap.authmode   = WIFI_AUTH_OPEN;                     // offenes Setup-Netz
    ap.ap.max_connection = 4;
    ap.ap.channel    = 1;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_dns_run = true;
    xTaskCreate(dns_task, "captive_dns", 4096, NULL, 5, &s_dns_task);
    http_start();

    app_set_state(ST_PROVISION);
    ui_set_pairing_info(SETUP_SSID);
    ESP_LOGI(TAG, "Captive Portal offen: SSID '%s' -> http://%s", SETUP_SSID, PORTAL_IP);
}

static void stop_portal(void)
{
    if (!s_portal_running) return;
    http_stop();
    s_dns_run = false;                 // dns_task beendet sich selbst (1s Timeout)
    s_portal_running = false;
    ESP_LOGI(TAG, "Captive Portal gestoppt");
}

static void connect_with_saved(void)
{
    const app_config_t *c = config_get();
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    wifi_config_t wc = {0};
    strncpy((char *)wc.sta.ssid,     c->wifi_ssid, sizeof wc.sta.ssid - 1);
    strncpy((char *)wc.sta.password, c->wifi_pass, sizeof wc.sta.password - 1);
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_connect();
    app_set_state(ST_CONNECTING);
    ESP_LOGI(TAG, "verbinde mit gespeicherter SSID '%s'", c->wifi_ssid);
}

static void wifi_stack_init_once(void)
{
    if (s_inited) return;
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t wi = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wi));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL);
    s_inited = true;
}

void provisioning_start(void)
{
    wifi_stack_init_once();

    if (config_is_provisioned()) {
        if (s_portal_running) stop_portal();
        connect_with_saved();
    } else {
        start_portal();
    }
}

void provisioning_reopen(void)
{
    // Re-Provisioning vom Settings-Screen (BOOT lang).
    esp_wifi_disconnect();
    if (!s_portal_running) start_portal();
}

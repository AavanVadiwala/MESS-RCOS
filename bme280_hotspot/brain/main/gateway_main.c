#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sdkconfig.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "uplink.h"

/* The sensor node joins this hotspot and POSTs to http://192.168.4.1/readings.
 * Each accepted body is printed as one USB serial line for the laptop bridge;
 * the bridge, not this gateway, validates the reading. The gateway also keeps
 * the last HISTORY readings so a phone or laptop on the hotspot can graph them
 * at http://192.168.4.1/ with no computer plugged in. */
static const char *TAG = "GATEWAY";
#define MAX_BODY 512
#define HISTORY 1200 /* shared by all sensors: 10 minutes for two at one reading a second each */

typedef struct {
    uint32_t received_ms, sequence;
    float temperature, humidity, pressure, lux, tvoc, eco2; /* NAN = the sensor sent no value */
    int rssi;
    char node[13], boot[9];
} reading_t;

/* Only the single httpd task touches these, so no lock is needed. */
static reading_t history[HISTORY];
static uint32_t next_id = 1; /* id given to the next stored reading; ids start at 1 */

extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[] asm("_binary_index_html_end");

static uint32_t uptime_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static bool json_number(const char *body, const char *key, double *out)
{
    char pattern[32];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    const char *at = strstr(body, pattern);
    if (!at) return false;
    at += strlen(pattern);
    char *end;
    *out = strtod(at, &end);
    return end != at && isfinite(*out);
}

/* Copies an alphanumeric string value; anything else ends it, so the copy is safe to echo as JSON. */
static void json_id(const char *body, const char *key, char *out, size_t size)
{
    char pattern[32];
    snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
    const char *at = strstr(body, pattern);
    size_t n = 0;
    if (at) for (at += strlen(pattern); n + 1 < size && isalnum((unsigned char)at[n]); n++) out[n] = at[n];
    out[n] = '\0';
}

/* A JSON number as a float, or NAN when the field is missing (that sensor sent nothing). */
static float json_value(const char *body, const char *key)
{
    double v;
    return json_number(body, key, &v) ? (float)v : NAN;
}

/* Stores a reading for the onboard page; the USB line is sent whatever happens here. */
static void remember(const char *body)
{
    double seq, rssi;
    if (!json_number(body, "sequence", &seq)) return;
    if (!json_number(body, "rssi_dbm", &rssi)) rssi = -127;
    reading_t r = {.received_ms = uptime_ms(), .sequence = (uint32_t)seq, .rssi = (int)rssi,
                   .temperature = json_value(body, "temperature_c"), .humidity = json_value(body, "humidity_pct"),
                   .pressure = json_value(body, "pressure_hpa"), .lux = json_value(body, "lux"),
                   .tvoc = json_value(body, "tvoc_ppb"), .eco2 = json_value(body, "eco2_ppm")};
    if (!isfinite(r.temperature) && !isfinite(r.humidity) && !isfinite(r.pressure) &&
        !isfinite(r.lux) && !isfinite(r.tvoc) && !isfinite(r.eco2)) return;
    json_id(body, "node_id", r.node, sizeof(r.node));
    json_id(body, "boot_id", r.boot, sizeof(r.boot));
    history[(next_id - 1) % HISTORY] = r;
    next_id++;
    uplink_add(r.node, r.boot, r.sequence, (const float[]){r.temperature, r.humidity, r.pressure, r.lux, r.tvoc, r.eco2}, r.rssi);
}

/* Appends a number with the given decimals, or null for NAN. */
static int put_value(char *out, size_t size, float v, int decimals)
{
    return isfinite(v) ? snprintf(out, size, ",%.*f", decimals, v) : snprintf(out, size, ",null");
}

static esp_err_t readings_post(httpd_req_t *req)
{
    char body[MAX_BODY + 1];
    if (req->content_len == 0 || req->content_len > MAX_BODY) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Body must be 1..512 bytes");
        return ESP_FAIL;
    }
    size_t received = 0;
    while (received < req->content_len) {
        int n = httpd_req_recv(req, body + received, req->content_len - received);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (n <= 0) return ESP_FAIL;
        received += n;
    }
    body[received] = '\0';
    /* Printable ASCII only, so one reading can never break the line framing. */
    for (size_t i = 0; i < received; i++) {
        if (body[i] < 0x20 || body[i] > 0x7e) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Printable ASCII only");
            return ESP_FAIL;
        }
    }
    printf("MESS_BME_JSON %s\n", body);
    fflush(stdout);
    remember(body);
    httpd_resp_set_status(req, "201 Created");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"forwarded\":true}");
}

static esp_err_t page_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, index_html_start, index_html_end - index_html_start);
}

/* GET /data?after=ID returns stored readings newer than ID as
 * {"now_ms":..,"next":..,"readings":[[id,received_ms,"node","boot",sequence,t,h,p,rssi,lux,tvoc,eco2],...]},
 * with null for any value the sensor did not send.
 * received_ms and now_ms are gateway uptime, so the page can turn them into wall-clock times. */
static esp_err_t data_get(httpd_req_t *req)
{
    char query[32], value[12];
    uint32_t after = 0;
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK &&
        httpd_query_key_value(query, "after", value, sizeof(value)) == ESP_OK)
        after = strtoul(value, NULL, 10);
    uint32_t first = next_id > HISTORY ? next_id - HISTORY : 1;
    if (after >= first && after < next_id) first = after + 1;
    else if (after >= next_id) first = next_id; /* nothing new; a restarted gateway is seen from "next" */
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    char buf[1024];
    int len = snprintf(buf, sizeof(buf), "{\"now_ms\":%lu,\"next\":%lu,\"readings\":[",
                       (unsigned long)uptime_ms(), (unsigned long)next_id);
    for (uint32_t id = first; id < next_id; id++) {
        const reading_t *r = &history[(id - 1) % HISTORY];
        if (len > (int)sizeof(buf) - 200) {
            if (httpd_resp_send_chunk(req, buf, len) != ESP_OK) return ESP_FAIL;
            len = 0;
        }
        len += snprintf(buf + len, sizeof(buf) - len, "%s[%lu,%lu,\"%s\",\"%s\",%lu",
                        id == first ? "" : ",", (unsigned long)id, (unsigned long)r->received_ms, r->node, r->boot,
                        (unsigned long)r->sequence);
        len += put_value(buf + len, sizeof(buf) - len, r->temperature, 2);
        len += put_value(buf + len, sizeof(buf) - len, r->humidity, 2);
        len += put_value(buf + len, sizeof(buf) - len, r->pressure, 2);
        len += snprintf(buf + len, sizeof(buf) - len, ",%d", r->rssi);
        len += put_value(buf + len, sizeof(buf) - len, r->lux, 1);
        len += put_value(buf + len, sizeof(buf) - len, r->tvoc, 0);
        len += put_value(buf + len, sizeof(buf) - len, r->eco2, 0);
        len += snprintf(buf + len, sizeof(buf) - len, "]");
    }
    len += snprintf(buf + len, sizeof(buf) - len, "]}");
    if (httpd_resp_send_chunk(req, buf, len) != ESP_OK) return ESP_FAIL;
    return httpd_resp_send_chunk(req, NULL, 0);
}

/* GET /relay hands the page what it needs to upload readings through the visitor's own
 * internet (phones keep mobile data while on this internet-less hotspot). {} = relay off. */
static esp_err_t relay_get(httpd_req_t *req)
{
    char body[384];
    if (strlen(CONFIG_MESS_GW_SUPABASE_URL) && strlen(CONFIG_MESS_GW_SUPABASE_KEY) && strlen(CONFIG_MESS_GW_VISITOR_KEY))
        snprintf(body, sizeof(body), "{\"url\":\"%s\",\"key\":\"%s\",\"upload_key\":\"%s\"}",
                 CONFIG_MESS_GW_SUPABASE_URL, CONFIG_MESS_GW_SUPABASE_KEY, CONFIG_MESS_GW_VISITOR_KEY);
    else strcpy(body, "{}");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, body);
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base;
    if (id == WIFI_EVENT_AP_STACONNECTED) {
        const wifi_event_ap_staconnected_t *e = data;
        ESP_LOGI(TAG, "Node " MACSTR " joined", MAC2STR(e->mac));
    } else if (id == WIFI_EVENT_AP_STADISCONNECTED) {
        const wifi_event_ap_stadisconnected_t *e = data;
        ESP_LOGW(TAG, "Node " MACSTR " left (reason %d)", MAC2STR(e->mac), e->reason);
    }
}

void app_main(void)
{
    const char *ssid = CONFIG_MESS_GW_SSID, *password = CONFIG_MESS_GW_PASSWORD;
    if (strlen(ssid) < 1 || strlen(ssid) > 32 || strlen(password) < 8 || strlen(password) > 63) {
        ESP_LOGE(TAG, "Set a hotspot name (1..32) and password (8..63) under MESS BME280 gateway; not starting an open network");
        return;
    }
    esp_err_t err = nvs_flash_init(); /* Wi-Fi driver needs NVS for calibration data. */
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();
    bool online = uplink_init();
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL));
    wifi_config_t conf = {
        .ap = {
            .ssid_len = strlen(ssid),
            .channel = CONFIG_MESS_GW_CHANNEL,
            .max_connection = 8, /* sensors plus phones viewing the page; the ESP32 allows up to 10 */
            .authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {.required = false},
        },
    };
    memcpy(conf.ap.ssid, ssid, strlen(ssid));
    memcpy(conf.ap.password, password, strlen(password));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    /* With an uplink, the hotspot follows the internet network's channel; sensors find it again by scanning. */
    ESP_ERROR_CHECK(esp_wifi_set_mode(online ? WIFI_MODE_APSTA : WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &conf));
    ESP_ERROR_CHECK(esp_wifi_start());
    if (online) uplink_start();

    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true; /* browsers hold idle connections open; don't let them lock out the sensor */
    config.stack_size = 8192; /* handlers keep 0.5-1 KB buffers and format floats; the 4 KB default is too tight */
    ESP_ERROR_CHECK(httpd_start(&server, &config));
    const httpd_uri_t handlers[] = {
        {.uri = "/readings", .method = HTTP_POST, .handler = readings_post},
        {.uri = "/", .method = HTTP_GET, .handler = page_get},
        {.uri = "/data", .method = HTTP_GET, .handler = data_get},
        {.uri = "/relay", .method = HTTP_GET, .handler = relay_get},
    };
    for (size_t i = 0; i < sizeof(handlers) / sizeof(handlers[0]); i++)
        ESP_ERROR_CHECK(httpd_register_uri_handler(server, &handlers[i]));
    ESP_LOGI(TAG, "Hotspot \"%s\" on channel %d; nodes POST to http://192.168.4.1/readings", ssid, CONFIG_MESS_GW_CHANNEL);
    ESP_LOGI(TAG, "Join the hotspot and open http://192.168.4.1/ to see live graphs");
}

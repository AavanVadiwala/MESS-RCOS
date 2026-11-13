#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_crt_bundle.h"
#include "esp_eap_client.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "uplink.h"

static const char *TAG = "UPLINK";
#define BUFFERED 900      /* 15 minutes at one reading a second while the uplink is down */
#define BATCH 60          /* readings per HTTPS request; each is up to ~260 bytes of JSON */
#define PERIOD_MS 10000   /* upload interval */
#define ONLINE BIT0

typedef struct {
    uint32_t received_ms, sequence;
    float values[UPLINK_VALUES];
    int8_t rssi;
    char node[13], boot[9];
} queued_t;

static queued_t *queue; /* allocated only when the uplink is enabled */
static size_t head, count; /* oldest entry and number queued; guarded by lock */
static SemaphoreHandle_t lock;
static EventGroupHandle_t state;
static uint32_t dropped;

static uint32_t uptime_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void sta_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *e = data;
        xEventGroupClearBits(state, ONLINE);
        ESP_LOGW(TAG, "Not connected to %s (reason %d); retrying", CONFIG_MESS_GW_UPLINK_SSID, e->reason);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *e = data;
        ESP_LOGI(TAG, "Online via %s, IP " IPSTR, CONFIG_MESS_GW_UPLINK_SSID, IP2STR(&e->ip_info.ip));
        xEventGroupSetBits(state, ONLINE);
    }
}

bool uplink_init(void)
{
    if (!strlen(CONFIG_MESS_GW_UPLINK_SSID)) {
        ESP_LOGI(TAG, "No uplink network set; readings stay on USB and the hotspot page");
        return false;
    }
    if (!strlen(CONFIG_MESS_GW_SUPABASE_URL) || !strlen(CONFIG_MESS_GW_SUPABASE_KEY) || !strlen(CONFIG_MESS_GW_UPLOAD_KEY)) {
        ESP_LOGE(TAG, "Uplink network set but Supabase URL, key or upload key missing; uplink disabled");
        return false;
    }
    queue = calloc(BUFFERED, sizeof(queued_t));
    lock = xSemaphoreCreateMutex();
    state = xEventGroupCreate();
    configASSERT(queue && lock && state);
    esp_netif_create_default_wifi_sta();
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, sta_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, sta_event, NULL));
    return true;
}

void uplink_add(const char *node, const char *boot, uint32_t sequence, const float values[UPLINK_VALUES], int rssi)
{
    if (!lock) return;
    xSemaphoreTake(lock, portMAX_DELAY);
    if (count == BUFFERED) { /* full: overwrite the oldest */
        head = (head + 1) % BUFFERED;
        count--;
        dropped++;
    }
    queued_t *q = &queue[(head + count) % BUFFERED];
    *q = (queued_t){.received_ms = uptime_ms(), .sequence = sequence, .rssi = (int8_t)rssi};
    memcpy(q->values, values, sizeof(q->values));
    strlcpy(q->node, node, sizeof(q->node));
    strlcpy(q->boot, boot, sizeof(q->boot));
    count++;
    xSemaphoreGive(lock);
}

/* Builds {"upload_key":..,"readings":[..]} from the oldest n queued readings; returns length or -1. */
static int build_batch(char *out, size_t size, size_t n)
{
    uint32_t now = uptime_ms();
    int len = snprintf(out, size, "{\"upload_key\":\"%s\",\"readings\":[", CONFIG_MESS_GW_UPLOAD_KEY);
    for (size_t i = 0; i < n && len > 0 && len < (int)size; i++) {
        const queued_t *q = &queue[(head + i) % BUFFERED];
        static const struct { const char *key; int decimals; } fields[UPLINK_VALUES] = {
            {"temperature_c", 2}, {"humidity_pct", 2}, {"pressure_hpa", 2}, {"lux", 1}, {"tvoc_ppb", 0}, {"eco2_ppm", 0}};
        len += snprintf(out + len, size - len,
            "%s{\"node_id\":\"%s\",\"boot_id\":\"%s\",\"sequence\":%lu,\"age_ms\":%lu,\"rssi_dbm\":%d",
            i ? "," : "", q->node, q->boot, (unsigned long)q->sequence, (unsigned long)(now - q->received_ms), q->rssi);
        for (size_t f = 0; f < UPLINK_VALUES && len > 0 && len < (int)size; f++)
            if (isfinite(q->values[f]))
                len += snprintf(out + len, size - len, ",\"%s\":%.*f", fields[f].key, fields[f].decimals, q->values[f]);
        if (len > 0 && len < (int)size) len += snprintf(out + len, size - len, "}");
    }
    if (len > 0 && len < (int)size) len += snprintf(out + len, size - len, "]}");
    return len > 0 && len < (int)size ? len : -1;
}

static int post(const char *body, int length)
{
    char url[192];
    snprintf(url, sizeof(url), "%s/rest/v1/rpc/add_bme280_readings", CONFIG_MESS_GW_SUPABASE_URL);
    esp_http_client_config_t config = {.url = url, .timeout_ms = 10000,
                                       .crt_bundle_attach = esp_crt_bundle_attach};
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return -1;
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "apikey", CONFIG_MESS_GW_SUPABASE_KEY);
    esp_http_client_set_post_field(client, body, length);
    esp_err_t err = esp_http_client_perform(client);
    int status = err == ESP_OK ? esp_http_client_get_status_code(client) : -1;
    if (err != ESP_OK) ESP_LOGW(TAG, "Upload failed: %s", esp_err_to_name(err));
    esp_http_client_cleanup(client);
    return status;
}

static void upload_task(void *arg)
{
    (void)arg;
    const size_t size = BATCH * 280 + 128;
    char *body = malloc(size);
    configASSERT(body);
    for (;;) {
        xEventGroupWaitBits(state, ONLINE, pdFALSE, pdTRUE, portMAX_DELAY);
        for (;;) { /* drain the backlog one batch at a time */
            xSemaphoreTake(lock, portMAX_DELAY);
            size_t n = count < BATCH ? count : BATCH;
            int length = n ? build_batch(body, size, n) : 0;
            xSemaphoreGive(lock);
            if (n == 0 || length < 0) break;
            int status = post(body, length);
            if (status < 0 || status >= 500 || status == 429) break; /* network or server trouble: keep and retry */
            if (status >= 300) ESP_LOGE(TAG, "Supabase rejected %u readings (HTTP %d); check the keys and schema", (unsigned)n, status);
            else ESP_LOGI(TAG, "Uploaded %u readings", (unsigned)n);
            /* Sent (or permanently rejected): drop them so one bad batch can't block the rest. */
            xSemaphoreTake(lock, portMAX_DELAY);
            head = (head + n) % BUFFERED;
            count -= n;
            xSemaphoreGive(lock);
            if (n < BATCH) break;
        }
        if (dropped) { ESP_LOGW(TAG, "%lu readings dropped while offline (buffer full)", (unsigned long)dropped); dropped = 0; }
        vTaskDelay(pdMS_TO_TICKS(PERIOD_MS));
    }
}

static void connect_task(void *arg)
{
    (void)arg;
    for (;;) { /* reconnect forever; eduroam and phone hotspots both drop now and then */
        if (!(xEventGroupGetBits(state) & ONLINE)) esp_wifi_connect();
        vTaskDelay(pdMS_TO_TICKS(15000));
    }
}

void uplink_start(void)
{
    const char *ssid = CONFIG_MESS_GW_UPLINK_SSID;
    wifi_config_t conf = {0};
    strlcpy((char *)conf.sta.ssid, ssid, sizeof(conf.sta.ssid));
    if (strlen(CONFIG_MESS_GW_EAP_USERNAME)) {
        /* WPA2-Enterprise (eduroam): PEAP/MSCHAPv2, and only to a server proving it is RPI's. */
        const char *identity = strlen(CONFIG_MESS_GW_EAP_IDENTITY) ? CONFIG_MESS_GW_EAP_IDENTITY : CONFIG_MESS_GW_EAP_USERNAME;
        ESP_ERROR_CHECK(esp_eap_client_set_identity((const unsigned char *)identity, strlen(identity)));
        ESP_ERROR_CHECK(esp_eap_client_set_username((const unsigned char *)CONFIG_MESS_GW_EAP_USERNAME, strlen(CONFIG_MESS_GW_EAP_USERNAME)));
        ESP_ERROR_CHECK(esp_eap_client_set_password((const unsigned char *)CONFIG_MESS_GW_EAP_PASSWORD, strlen(CONFIG_MESS_GW_EAP_PASSWORD)));
        /* Verify the server against the built-in public root certificates (the same kind browsers use).
         * RPI's eduroam profile on cat.eduroam.org lists roots its current certificate no longer uses. */
        ESP_ERROR_CHECK(esp_eap_client_use_default_cert_bundle(true));
        if (strlen(CONFIG_MESS_GW_EAP_SERVER_NAME))
            ESP_ERROR_CHECK(esp_eap_client_set_domain_name(CONFIG_MESS_GW_EAP_SERVER_NAME));
        ESP_ERROR_CHECK(esp_wifi_sta_enterprise_enable());
        conf.sta.threshold.authmode = WIFI_AUTH_WPA2_ENTERPRISE;
        ESP_LOGI(TAG, "Joining %s as %s (WPA2-Enterprise, server %s)", ssid, CONFIG_MESS_GW_EAP_USERNAME, CONFIG_MESS_GW_EAP_SERVER_NAME);
    } else {
        strlcpy((char *)conf.sta.password, CONFIG_MESS_GW_UPLINK_PASSWORD, sizeof(conf.sta.password));
        conf.sta.threshold.authmode = strlen(CONFIG_MESS_GW_UPLINK_PASSWORD) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
        ESP_LOGI(TAG, "Joining %s", ssid);
    }
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &conf));
    configASSERT(xTaskCreate(connect_task, "uplink_conn", 3072, NULL, 3, NULL) == pdPASS);
    configASSERT(xTaskCreate(upload_task, "uplink", 8192, NULL, 3, NULL) == pdPASS);
}

#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "wifi_sender.h"

static const char *TAG = "WIFI";
static QueueHandle_t latest;
static EventGroupHandle_t network;
static char node_id[13];
static uint32_t boot_id;
#define WIFI_READY BIT0
typedef struct { uint32_t sequence; float temperature, humidity, pressure; } sample_t;

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)data;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(network, WIFI_READY);
        ESP_LOGW(TAG, "Disconnected from %s; retrying", CONFIG_MESS_BME_WIFI_SSID);
    }
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *got = data;
        ESP_LOGI(TAG, "Connected, IP " IPSTR "; sending to %s", IP2STR(&got->ip_info.ip), CONFIG_MESS_BME_SERVER_URL);
        xEventGroupSetBits(network, WIFI_READY);
    }
}

static bool start_wifi(void)
{
    const char *ssid = CONFIG_MESS_BME_WIFI_SSID, *password = CONFIG_MESS_BME_WIFI_PASSWORD;
    if (strlen(ssid) > 32 || strlen(password) > 63) {
        ESP_LOGE(TAG, "Wi-Fi credentials exceed supported lengths");
        return false;
    }
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL));
    wifi_config_t conf = {0};
    memcpy(conf.sta.ssid, ssid, strlen(ssid));
    memcpy(conf.sta.password, password, strlen(password));
    conf.sta.threshold.authmode = strlen(password) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &conf));
    ESP_ERROR_CHECK(esp_wifi_start());
    return true;
}

static void sender_task(void *arg)
{
    (void)arg;
    if (!start_wifi()) { vTaskDelete(NULL); return; }
    for (;;) {
        if (!(xEventGroupGetBits(network) & WIFI_READY)) {
            esp_wifi_connect();
            /* Wait for an IP rather than hammering connect(); a phone hotspot can take a while. */
            xEventGroupWaitBits(network, WIFI_READY, pdFALSE, pdTRUE, pdMS_TO_TICKS(10000));
            continue;
        }
        sample_t s;
        if (xQueueReceive(latest, &s, pdMS_TO_TICKS(1000)) != pdTRUE) continue;
        wifi_ap_record_t ap = {0};
        int rssi = esp_wifi_sta_get_ap_info(&ap) == ESP_OK ? ap.rssi : -127;
        char json[256];
        snprintf(json, sizeof(json),
            "{\"node_id\":\"%s\",\"boot_id\":\"%08lx\",\"sequence\":%lu,\"temperature_c\":%.2f,"
            "\"humidity_pct\":%.2f,\"pressure_hpa\":%.2f,\"rssi_dbm\":%d}",
            node_id, (unsigned long)boot_id, (unsigned long)s.sequence,
            s.temperature, s.humidity, s.pressure, rssi);
        esp_http_client_config_t cfg = {.url = CONFIG_MESS_BME_SERVER_URL, .timeout_ms = 2000,
                                        .disable_auto_redirect = true};
        esp_http_client_handle_t client = esp_http_client_init(&cfg);
        if (!client) { ESP_LOGE(TAG, "HTTP allocation failed"); continue; }
        esp_http_client_set_method(client, HTTP_METHOD_POST);
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, json, (int)strlen(json));
        esp_err_t err = esp_http_client_perform(client);
        int status = esp_http_client_get_status_code(client);
        if (err != ESP_OK || status < 200 || status >= 300)
            ESP_LOGW(TAG, "Send failed: %s, HTTP %d; sample dropped", esp_err_to_name(err), status);
        esp_http_client_cleanup(client);
        /* No backlog: the one-element mailbox keeps only the newest sample. */
    }
}

bool mess_wifi_start(void)
{
    if (!strlen(CONFIG_MESS_BME_WIFI_SSID)) {
        ESP_LOGI(TAG, "Serial-only mode: set Wi-Fi SSID under MESS BME280 to send readings wirelessly");
        return false;
    }
    esp_err_t err = nvs_flash_init(); /* Wi-Fi driver needs NVS for calibration data. */
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_STA));
    snprintf(node_id, sizeof(node_id), "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    boot_id = esp_random();
    latest = xQueueCreate(1, sizeof(sample_t));
    network = xEventGroupCreate();
    configASSERT(latest && network);
    configASSERT(xTaskCreate(sender_task, "sender", 6144, NULL, 3, NULL) == pdPASS);
    return true;
}

void mess_wifi_submit(float temperature, float humidity, float pressure)
{
    static uint32_t sequence;
    if (!latest) return;
    sample_t s = {sequence++, temperature, humidity, pressure};
    xQueueOverwrite(latest, &s);
}

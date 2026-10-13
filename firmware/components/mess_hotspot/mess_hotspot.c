#include "mess_hotspot.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_http_server.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

typedef struct {
    mess_mesh_record_t record;
    uint8_t relay[6];
    int rssi;
    bool local;
    uint64_t received_us, number;
} row_t;
static row_t history[MESS_HOTSPOT_HISTORY];
static uint64_t total;
static SemaphoreHandle_t history_lock;
static httpd_handle_t server;
extern const unsigned char index_start[] asm("_binary_index_html_start");
extern const unsigned char index_end[] asm("_binary_index_html_end");

static void mac_text(const uint8_t mac[6], char text[18])
{
    snprintf(text, 18, "%02x:%02x:%02x:%02x:%02x:%02x",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void mess_hotspot_collect(const mess_mesh_record_t *record,
    const uint8_t relay[6], int rssi, bool local)
{
    if (!history_lock || !record || !relay || record->type != MESS_MESH_READING ||
        record->sensor > MESS_SENSOR_BH1750 || record->length > MESS_MESH_MAX_TEXT) return;
    xSemaphoreTake(history_lock, portMAX_DELAY);
    row_t *row = &history[total % MESS_HOTSPOT_HISTORY];
    *row = (row_t){.record = *record, .rssi = rssi, .local = local,
        .received_us = (uint64_t)esp_timer_get_time(), .number = total};
    memcpy(row->relay, relay, 6);
    ++total;
    xSemaphoreGive(history_lock);
}

static esp_err_t page(httpd_req_t *request)
{
    httpd_resp_set_type(request, "text/html");
    return httpd_resp_send(request, (const char *)index_start, index_end - index_start - 1);
}

/* Protocol text is printable ASCII; JSON still requires escaping quotes and slashes. */
static void escape_text(const char *input, size_t length, char *output)
{
    size_t used = 0;
    for (size_t i = 0; i < length; ++i) {
        if (input[i] == '"' || input[i] == '\\') output[used++] = '\\';
        output[used++] = input[i];
    }
    output[used] = 0;
}

static esp_err_t readings(httpd_req_t *request)
{
    xSemaphoreTake(history_lock, portMAX_DELAY);
    uint64_t end = total;
    xSemaphoreGive(history_lock);
    uint64_t begin = end > MESS_HOTSPOT_HISTORY ? end - MESS_HOTSPOT_HISTORY : 0;
    char opening[160];
    snprintf(opening, sizeof(opening), "{\"total\":\"%" PRIu64 "\",\"capacity\":%u,\"rows\":[",
        end, MESS_HOTSPOT_HISTORY);
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    esp_err_t error = httpd_resp_sendstr_chunk(request, opening);
    if (error != ESP_OK) return error;
    bool first = true;
    for (uint64_t number = begin; number < end; ++number) {
        row_t row;
        xSemaphoreTake(history_lock, portMAX_DELAY);
        row = history[number % MESS_HOTSPOT_HISTORY];
        xSemaphoreGive(history_lock);
        if (row.number != number) continue; /* Replaced while streaming; never mix rows. */
        char mac[18], relay[18], uuid[64], timestamp[32], session[32], received[32];
        mac_text(row.record.origin, mac); mac_text(row.relay, relay);
        snprintf(timestamp, sizeof(timestamp), "%" PRIu64, row.record.measured_us);
        snprintf(session, sizeof(session), "%" PRIu64, row.record.session);
        snprintf(received, sizeof(received), "%" PRIu64, row.received_us);
        snprintf(uuid, sizeof(uuid), "%s-%s", mac, timestamp);
        const char *names[] = {"BME280", "SGP30", "BH1750"};
        char escaped[MESS_MESH_MAX_TEXT * 2 + 1], encoded[1024], rssi[16];
        escape_text(row.record.text, row.record.length, escaped);
        if (row.local) strcpy(rssi, "null");
        else snprintf(rssi, sizeof(rssi), "%d", row.rssi);
        int size = snprintf(encoded, sizeof(encoded),
            "{\"uuid\":\"%s\",\"mac\":\"%s\",\"sensor\":\"%s\","
            "\"time_us\":\"%s\",\"session\":\"%s\",\"sequence\":%" PRIu32 ","
            "\"reading\":\"%s\",\"via\":\"%s\",\"hops\":%u,"
            "\"received_us\":\"%s\",\"rssi\":%s}",
            uuid, mac, names[row.record.sensor], timestamp, session,
            row.record.sequence, escaped, row.local ? "local" : relay,
            row.record.hops, received, rssi);
        if (size < 0 || size >= (int)sizeof(encoded)) return ESP_ERR_INVALID_SIZE;
        if (!first) error = httpd_resp_sendstr_chunk(request, ",");
        if (error == ESP_OK) error = httpd_resp_sendstr_chunk(request, encoded);
        if (error != ESP_OK) return error;
        first = false;
    }
    error = httpd_resp_sendstr_chunk(request, "]}");
    return error == ESP_OK ? httpd_resp_send_chunk(request, NULL, 0) : error;
}

esp_err_t mess_hotspot_prepare(void)
{
    if (history_lock) return ESP_ERR_INVALID_STATE;
    history_lock = xSemaphoreCreateMutex();
    if (!history_lock) return ESP_ERR_NO_MEM;
    if (!esp_netif_create_default_wifi_ap()) return ESP_ERR_NO_MEM;
    uint8_t mac[6];
    esp_err_t error = esp_wifi_get_mac(WIFI_IF_STA, mac);
    if (error != ESP_OK) return error;
    wifi_config_t config = {0};
    snprintf((char *)config.ap.ssid, sizeof(config.ap.ssid),
        "MESS-%02X%02X%02X%02X%02X%02X",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    config.ap.ssid_len = strlen((char *)config.ap.ssid);
    memcpy(config.ap.password, MESS_HOTSPOT_PASSWORD, sizeof(MESS_HOTSPOT_PASSWORD));
    config.ap.channel = 1;
    config.ap.max_connection = 4;
    config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    error = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (error != ESP_OK) return error;
    /* Ordinary laptop Wi-Fi on AP; LR is enabled separately on STA. */
    error = esp_wifi_set_protocol(WIFI_IF_AP,
        WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
    return error == ESP_OK ? esp_wifi_set_config(WIFI_IF_AP, &config) : error;
}

esp_err_t mess_hotspot_start(void)
{
    if (!history_lock || server) return ESP_ERR_INVALID_STATE;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 6144;
    esp_err_t error = httpd_start(&server, &config);
    if (error != ESP_OK) return error;
    const httpd_uri_t home = {.uri = "/", .method = HTTP_GET, .handler = page};
    const httpd_uri_t api = {.uri = "/api/readings", .method = HTTP_GET, .handler = readings};
    error = httpd_register_uri_handler(server, &home);
    if (error == ESP_OK) error = httpd_register_uri_handler(server, &api);
    if (error != ESP_OK) { httpd_stop(server); server = NULL; return error; }
    printf("MESS hotspot password: %s | Table: http://192.168.4.1/\n", MESS_HOTSPOT_PASSWORD);
    return ESP_OK;
}

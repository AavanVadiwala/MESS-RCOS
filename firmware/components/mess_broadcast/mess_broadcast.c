#include "mess_broadcast.h"
#include "mess_mesh.h"
#include "mess_serial_conn.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mesh.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <stdio.h>
#include "sdkconfig.h"
#include <stdatomic.h>
#include <string.h>

static const char *TAG = "mess_broadcast";
static uint8_t own_mac[6];
static uint64_t own_session;
static atomic_uint sequence;
static atomic_bool connected;
static bool started;
static bool collector;

static esp_err_t select_role(void)
{
    nvs_handle_t settings;
    esp_err_t error = nvs_open("mess_mesh", NVS_READWRITE, &settings);
    if (error != ESP_OK) return error;
    uint8_t saved = 0;
    error = nvs_get_u8(settings, "collector", &saved);
    if (error == ESP_ERR_NVS_NOT_FOUND) error = ESP_OK;
    if (error == ESP_OK) {
        puts("Send collector or node followed by Enter within 5 seconds to set the mesh role.");
        fflush(stdout);
        char command[16];
        size_t used = 0;
        int64_t deadline = esp_timer_get_time() + 5000000;
        while (esp_timer_get_time() < deadline) {
            int c = getchar();
            if (c == EOF) {
                clearerr(stdin);
                vTaskDelay(pdMS_TO_TICKS(20));
                continue;
            }
            if (c == '\r' || c == '\n') {
                if (used < sizeof(command)) {
                    command[used] = '\0';
                    if (!strcmp(command, "collector") || !strcmp(command, "node")) {
                        saved = !strcmp(command, "collector");
                        error = nvs_set_u8(settings, "collector", saved);
                        if (error == ESP_OK) error = nvs_commit(settings);
                        break;
                    }
                }
                used = 0;
            } else if (used < sizeof(command)) {
                command[used++] = (char)c;
            }
        }
    }
    nvs_close(settings);
    collector = saved != 0;
    if (error == ESP_OK) printf("MESS mesh role: %s\n", collector ? "collector" : "node");
    return error;
}

static void mesh_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)data;
    if (id == MESH_EVENT_PARENT_CONNECTED) atomic_store(&connected, true);
    if (id == MESH_EVENT_PARENT_DISCONNECTED || id == MESH_EVENT_STOPPED)
        atomic_store(&connected, false);
}

static void collect(void *argument)
{
    (void)argument;
    uint8_t frame[MESS_MESH_MAX_FRAME];
    for (;;) {
        mesh_addr_t source;
        mesh_data_t packet = {.data = frame, .size = sizeof(frame)};
        int flags = 0;
        esp_err_t error = esp_mesh_recv(&source, &packet, 1000, &flags, NULL, 0);
        if (error != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        mess_mesh_record_t record;
        if (mess_mesh_decode(frame, packet.size, &record) &&
            record.type == MESS_MESH_READING &&
            !memcmp(record.origin, source.addr, sizeof(record.origin))) {
            mess_serial_conn_write(&record);
        }
    }
}

esp_err_t mess_broadcast_publish(uint8_t sensor, uint64_t measured_us,
    const char *text, size_t length)
{
    if (!started) return ESP_ERR_INVALID_STATE;
    uint64_t now = (uint64_t)esp_timer_get_time();
    if (!text || !length || length > MESS_BROADCAST_MAX_TEXT ||
        sensor > MESS_SENSOR_BH1750 || measured_us > now ||
        (now - measured_us) / 1000 >= MESS_MESH_LIFETIME_MS)
        return ESP_ERR_INVALID_ARG;
    for (size_t i = 0; i < length; ++i)
        if ((unsigned char)text[i] < 32 || (unsigned char)text[i] > 126)
            return ESP_ERR_INVALID_ARG;

    mess_mesh_record_t record = {.type = MESS_MESH_READING, .sensor = sensor,
        .session = own_session, .measured_us = measured_us, .length = length,
        .age_ms = (uint32_t)((now - measured_us) / 1000),
        .sequence = atomic_fetch_add(&sequence, 1) + 1};
    memcpy(record.origin, own_mac, sizeof(own_mac));
    memcpy(record.text, text, length);
    if (collector) {
        mess_serial_conn_write(&record);
        return ESP_OK;
    }
    if (!atomic_load(&connected)) return ESP_ERR_INVALID_STATE;
    uint8_t frame[MESS_MESH_MAX_FRAME];
    size_t size = mess_mesh_encode(&record, frame, sizeof(frame));
    if (!size) return ESP_ERR_INVALID_ARG;
    mesh_data_t packet = {.data = frame, .size = size,
        .proto = MESH_PROTO_BIN, .tos = MESH_TOS_P2P};
    return esp_mesh_send(NULL, &packet, MESH_DATA_P2P, NULL, 0);
}

esp_err_t mess_broadcast_start(void)
{
    if (started) return ESP_ERR_INVALID_STATE;
    mesh_cfg_t mesh = MESH_INIT_CONFIG_DEFAULT();
    size_t mesh_pass_len = strlen(CONFIG_MESS_MESH_AP_PASSWORD);
    if (mesh_pass_len < 8 || mesh_pass_len >= sizeof(mesh.mesh_ap.password)) {
        ESP_LOGE(TAG, "Configure the mesh link password in menuconfig");
        return ESP_ERR_INVALID_ARG;
    }
    const uint8_t mesh_id[6] = {0x02, 0x4d, 0x45, 0x53, 0x53, 0x01};
    memcpy(mesh.mesh_id.addr, mesh_id, sizeof(mesh_id));
    mesh.channel = CONFIG_MESS_MESH_CHANNEL;
    mesh.allow_channel_switch = false;
    mesh.router.allow_router_switch = false;
    memcpy(mesh.mesh_ap.password, CONFIG_MESS_MESH_AP_PASSWORD, mesh_pass_len);
    mesh.mesh_ap.max_connection = 6;

    esp_err_t error = nvs_flash_init();
    if (error != ESP_OK) return error;
    ESP_ERROR_CHECK(select_role());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t wifi = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    const uint8_t protocols = WIFI_PROTOCOL_LR;
    ESP_ERROR_CHECK(esp_wifi_set_protocol(WIFI_IF_STA, protocols));
    ESP_ERROR_CHECK(esp_wifi_set_protocol(WIFI_IF_AP, protocols));
    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_STA, own_mac));
    ESP_ERROR_CHECK(esp_wifi_start());
    own_session = ((uint64_t)esp_random() << 32) | esp_random();
    if (!own_session) own_session = 1;

    ESP_ERROR_CHECK(esp_mesh_init());
    ESP_ERROR_CHECK(esp_event_handler_register(MESH_EVENT, ESP_EVENT_ANY_ID, mesh_event, NULL));
    ESP_ERROR_CHECK(esp_mesh_set_ap_authmode(WIFI_AUTH_WPA2_PSK));
    if (collector) {
        ESP_ERROR_CHECK(esp_mesh_set_type(MESH_ROOT));
    } else {
        ESP_ERROR_CHECK(esp_mesh_fix_root(true));
    }
    ESP_ERROR_CHECK(esp_mesh_set_config(&mesh));
    ESP_ERROR_CHECK(esp_mesh_send_block_time(1000));
    ESP_ERROR_CHECK(esp_mesh_start());
    if (collector) {
        ESP_ERROR_CHECK(xTaskCreate(collect, "mesh_serial", 4096, NULL, 2, NULL)
            == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    }
    started = true;
    return ESP_OK;
}

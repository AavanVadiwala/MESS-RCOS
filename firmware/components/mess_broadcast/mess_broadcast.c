#include "mess_broadcast.h"
#include "mess_mesh.h"
#include "mess_hotspot.h"
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#define MAX_NEIGHBORS 32
#define EXPIRE_MS 6000
static const uint8_t broadcast[6] = {255, 255, 255, 255, 255, 255};
static uint8_t own_mac[6];
static uint64_t own_session;
static uint32_t sequence;
static QueueHandle_t incoming;
static SemaphoreHandle_t publish_lock;
static bool started;
/* Cache lives in static storage, not the worker's stack, and has one owner. */
static mess_mesh_cache_t cache;
typedef struct {
    uint8_t mac[6];
    int rssi;
    bool local;
    uint64_t received_ms;
    mess_mesh_record_t record;
} packet_t;
typedef struct {
    bool active;
    uint8_t mac[6];
    uint64_t seen_ms;
} neighbor_t;

static uint64_t now_ms(void) { return (uint64_t)esp_timer_get_time() / 1000; }

static void print_mac(const uint8_t mac[6])
{
    printf("%02x:%02x:%02x:%02x:%02x:%02x",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/* Wi-Fi callbacks only validate/copy into a bounded queue; no printing or I2C. */
static void received(const esp_now_recv_info_t *info, const uint8_t *data, int length)
{
    if (!info || !info->rx_ctrl || !info->src_addr || !data || length < 0 ||
        !memcmp(info->src_addr, own_mac, 6)) return;
    packet_t packet = {.rssi = info->rx_ctrl->rssi, .received_ms = now_ms()};
    if (!mess_mesh_decode(data, (size_t)length, &packet.record)) return;
    if (packet.record.type == MESS_MESH_HELLO &&
        memcmp(packet.record.origin, info->src_addr, 6)) return;
    if (packet.record.type == MESS_MESH_READING && !packet.record.hops &&
        memcmp(packet.record.origin, info->src_addr, 6)) return;
    memcpy(packet.mac, info->src_addr, 6);
    xQueueSend(incoming, &packet, 0);
}

/* Only the mesh worker submits radio packets; sensors enqueue local records. */
static esp_err_t send_record(const mess_mesh_record_t *record)
{
    uint8_t frame[MESS_MESH_MAX_FRAME];
    size_t length = mess_mesh_encode(record, frame, sizeof(frame));
    return length ? esp_now_send(broadcast, frame, length) : ESP_ERR_INVALID_ARG;
}

esp_err_t mess_broadcast_publish(uint8_t sensor, uint64_t measured_us,
    const char *text, size_t length)
{
    if (!started) return ESP_ERR_INVALID_STATE;
    if (!text || !length || length > MESS_BROADCAST_MAX_TEXT ||
        sensor > MESS_SENSOR_BH1750 || measured_us > (uint64_t)esp_timer_get_time())
        return ESP_ERR_INVALID_ARG;
    for (size_t i = 0; i < length; ++i)
        if ((unsigned char)text[i] < 32 || (unsigned char)text[i] > 126)
            return ESP_ERR_INVALID_ARG;
    packet_t packet = {.local = true, .received_ms = now_ms(),
        .record = {.type = MESS_MESH_READING, .sensor = sensor,
            .session = own_session, .measured_us = measured_us, .length = length}};
    uint64_t age = packet.received_ms - measured_us / 1000;
    if (age >= MESS_MESH_LIFETIME_MS) return ESP_ERR_INVALID_ARG;
    packet.record.age_ms = (uint32_t)age;
    memcpy(packet.mac, own_mac, 6);
    memcpy(packet.record.origin, own_mac, 6);
    memcpy(packet.record.text, text, length);
    xSemaphoreTake(publish_lock, portMAX_DELAY);
    packet.record.sequence = ++sequence;
    bool queued = xQueueSend(incoming, &packet, pdMS_TO_TICKS(100)) == pdTRUE;
    xSemaphoreGive(publish_lock);
    return queued ? ESP_OK : ESP_ERR_TIMEOUT;
}

static void discover(void *argument)
{
    (void)argument;
    neighbor_t neighbors[MAX_NEIGHBORS] = {0};
    uint64_t last_hello = now_ms();
    uint32_t interval = 200 + esp_random() % 800;
    for (;;) {
        packet_t packet;
        bool got = xQueueReceive(incoming, &packet, pdMS_TO_TICKS(20)) == pdTRUE;
        uint64_t now = now_ms();
        for (unsigned i = 0; i < MAX_NEIGHBORS; ++i) {
            if (neighbors[i].active && now - neighbors[i].seen_ms >= EXPIRE_MS) {
                printf("Neighbor expired: "); print_mac(neighbors[i].mac); putchar('\n');
                neighbors[i].active = false;
            }
        }
        if (got) {
            if (!packet.local) {
                int found = -1, available = -1;
                for (unsigned i = 0; i < MAX_NEIGHBORS; ++i) {
                    if (neighbors[i].active && !memcmp(neighbors[i].mac, packet.mac, 6)) found = i;
                    if (!neighbors[i].active && available < 0) available = i;
                }
                if (found < 0 && available >= 0) {
                    found = available;
                    neighbors[found].active = true;
                    memcpy(neighbors[found].mac, packet.mac, 6);
                    printf("Neighbor detected: "); print_mac(packet.mac);
                    printf(" RSSI=%d dBm\n", packet.rssi);
                }
                if (found >= 0) neighbors[found].seen_ms = now;
            }
            if (packet.record.type == MESS_MESH_READING &&
                (packet.local || memcmp(packet.record.origin, own_mac, 6))) {
                uint64_t age = packet.record.age_ms + (now - packet.received_ms);
                if (age < MESS_MESH_LIFETIME_MS) {
                    packet.record.age_ms = (uint32_t)age;
                    uint32_t delay = 20 + esp_random() % 181;
                    mess_mesh_result_t result = mess_mesh_accept(&cache, &packet.record, now, delay);
                    if (result == MESS_MESH_ACCEPTED) {
                        mess_hotspot_collect(&packet.record, packet.mac, packet.rssi, packet.local);
                        printf("%s origin=", packet.local ? "Local" : "Remote");
                        print_mac(packet.record.origin);
                        printf(" session=%" PRIu64 " seq=%" PRIu32 " sensor=%u time_us=%" PRIu64
                            " hops=%u age_ms=%" PRIu32,
                            packet.record.session, packet.record.sequence, packet.record.sensor,
                            packet.record.measured_us, packet.record.hops, packet.record.age_ms);
                        if (!packet.local) {
                            printf(" via="); print_mac(packet.mac);
                            printf(" link_RSSI=%d dBm", packet.rssi);
                        }
                        printf(": %.*s\n", (int)packet.record.length, packet.record.text);
                    } else if (result == MESS_MESH_FULL) {
                        puts("Mesh cache full; reading dropped.");
                    }
                }
            }
        }
        mess_mesh_record_t outgoing;
        /* Pace submissions and process arrivals between sends, allowing supersession. */
        if (mess_mesh_next(&cache, now, own_mac, &outgoing)) {
            esp_err_t error = send_record(&outgoing);
            if (error != ESP_OK) printf("Mesh submission: %s\n", esp_err_to_name(error));
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (now - last_hello >= interval) {
            mess_mesh_record_t hello = {.type = MESS_MESH_HELLO, .sensor = 255,
                .session = own_session};
            memcpy(hello.origin, own_mac, 6);
            esp_err_t error = send_record(&hello);
            if (error != ESP_OK) printf("Discovery submission: %s\n", esp_err_to_name(error));
            last_hello = now;
            interval = 1000 + esp_random() % 1000;
        }
    }
}

esp_err_t mess_broadcast_start(void)
{
    if (started) return ESP_ERR_INVALID_STATE;
    esp_err_t error = nvs_flash_init();
    if (error == ESP_ERR_NVS_NO_FREE_PAGES || error == ESP_ERR_NVS_NEW_VERSION_FOUND)
        return error; /* Preserve NVS; caller chooses recovery policy. */
    ESP_ERROR_CHECK(error);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&config));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(mess_hotspot_prepare());
    ESP_ERROR_CHECK(esp_wifi_set_protocol(WIFI_IF_STA,
        WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N | WIFI_PROTOCOL_LR));
    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_STA, own_mac));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    own_session = ((uint64_t)esp_random() << 32) | esp_random();
    if (!own_session) own_session = 1;
    incoming = xQueueCreate(32, sizeof(packet_t));
    publish_lock = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(incoming && publish_lock ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(received));
    esp_now_peer_info_t peer = {.channel = 1, .ifidx = WIFI_IF_STA, .encrypt = false};
    memcpy(peer.peer_addr, broadcast, 6);
    ESP_ERROR_CHECK(esp_now_add_peer(&peer));
    esp_now_rate_config_t rate = {
        .phymode = WIFI_PHY_MODE_LR, .rate = WIFI_PHY_RATE_LORA_500K
    };
    ESP_ERROR_CHECK(esp_now_set_peer_rate_config(broadcast, &rate));
    ESP_ERROR_CHECK(mess_hotspot_start());
    ESP_ERROR_CHECK(xTaskCreate(discover, "mess_mesh", 4096, NULL, 2, NULL)
        == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    started = true;
    puts("ESP-NOW mesh: channel 1, Long Range 500 Kbps, latest readings, max 8 relay hops.");
    return ESP_OK;
}

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "esp_app_desc.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#define MAX_BOARDS 32
#define FRAME_SIZE 41
#define EXPIRE_MS 6000
static const uint8_t magic[8] = {'M','E','S','S','S','C','A','N'};
static const uint8_t broadcast[6] = {255,255,255,255,255,255};
typedef struct { uint8_t mac[6]; int rssi; } observation_t;
typedef struct {
    bool active;
    observation_t observation;
    TickType_t last_seen;
} board_t;
static QueueHandle_t observations, completion;
static board_t boards[MAX_BOARDS];
static uint8_t own_mac[6];

/* Callback data belongs to the driver: copy it and never block its task. */
static void received(const esp_now_recv_info_t *info, const uint8_t *data, int length)
{
    if (!info || !info->rx_ctrl || !data || length != FRAME_SIZE ||
        memcmp(data, magic, sizeof(magic)) || data[8] != 1 ||
        memcmp(data + 9, esp_app_get_description()->app_elf_sha256, 32) ||
        !memcmp(info->src_addr, own_mac, 6)) return;
    observation_t observation = {.rssi = info->rx_ctrl->rssi};
    memcpy(observation.mac, info->src_addr, 6);
    xQueueSend(observations, &observation, 0);
}

static void sent(const esp_now_send_info_t *info, esp_now_send_status_t status)
{
    (void)info;
    xQueueOverwrite(completion, &status);
}

static void expire_boards(TickType_t now)
{
    for (unsigned i = 0; i < MAX_BOARDS; ++i)
        if (boards[i].active && now - boards[i].last_seen >= pdMS_TO_TICKS(EXPIRE_MS))
            boards[i].active = false;
}

static void record(const observation_t *observation, TickType_t now)
{
    unsigned free_slot = MAX_BOARDS;
    for (unsigned i = 0; i < MAX_BOARDS; ++i) {
        if (!boards[i].active) { if (free_slot == MAX_BOARDS) free_slot = i; }
        else if (!memcmp(boards[i].observation.mac, observation->mac, 6)) {
            boards[i].observation = *observation;
            boards[i].last_seen = now;
            return;
        }
    }
    if (free_slot < MAX_BOARDS) {
        boards[free_slot] = (board_t){.active = true, .observation = *observation, .last_seen = now};
    }
}

static void print_boards(void)
{
    unsigned count = 0;
    puts("\nNearby MCUs (same firmware; channel 1):");
    for (unsigned i = 0; i < MAX_BOARDS; ++i) {
        if (!boards[i].active) continue;
        const observation_t *o = &boards[i].observation;
        printf("MCU %02x:%02x:%02x:%02x:%02x:%02x : TX rate 250 Kbps (configured) | RSSI %d dBm\n",
               o->mac[0], o->mac[1], o->mac[2], o->mac[3], o->mac[4], o->mac[5], o->rssi);
        ++count;
    }
    if (!count) puts("No matching MCU detected.");
    if (count == MAX_BOARDS) puts("List capacity: 32 MCUs.");
    fflush(stdout);
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&config));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_protocol(WIFI_IF_STA,
        WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N | WIFI_PROTOCOL_LR));
    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_STA, own_mac));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    observations = xQueueCreate(32, sizeof(observation_t));
    completion = xQueueCreate(1, sizeof(esp_now_send_status_t));
    ESP_ERROR_CHECK(observations && completion ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(received));
    ESP_ERROR_CHECK(esp_now_register_send_cb(sent));
    esp_now_peer_info_t peer = {.channel = 1, .ifidx = WIFI_IF_STA, .encrypt = false};
    memcpy(peer.peer_addr, broadcast, 6);
    ESP_ERROR_CHECK(esp_now_add_peer(&peer));
    esp_now_rate_config_t rate = {.phymode = WIFI_PHY_MODE_LR, .rate = WIFI_PHY_RATE_LORA_250K};
    ESP_ERROR_CHECK(esp_now_set_peer_rate_config(broadcast, &rate));

    uint8_t frame[FRAME_SIZE] = {0};
    memcpy(frame, magic, sizeof(magic));
    frame[8] = 1;
    memcpy(frame + 9, esp_app_get_description()->app_elf_sha256, 32);
    puts("Automatic MCU scanner: TX rate 250 Kbps (configured), RSSI in dBm.");
    puts("Distance is unavailable. Flash the same built image onto every MCU.");
    bool pending = false;
    TickType_t now = xTaskGetTickCount(), last_send = now, last_print = now;
    TickType_t send_interval = pdMS_TO_TICKS(200 + esp_random() % 1000);
    for (;;) {
        observation_t observation;
        bool got = xQueueReceive(observations, &observation, pdMS_TO_TICKS(100)) == pdTRUE;
        now = xTaskGetTickCount();
        expire_boards(now);
        if (got) record(&observation, now);
        esp_now_send_status_t status;
        if (pending && xQueueReceive(completion, &status, 0) == pdTRUE) pending = false;
        /* Wait for completion before submitting another broadcast. Random jitter
         * prevents boards flashed together from repeatedly transmitting together. */
        if (!pending && now - last_send >= send_interval) {
            esp_err_t err = esp_now_send(broadcast, frame, sizeof(frame));
            pending = err == ESP_OK;
            if (err != ESP_OK) printf("Announcement failed: %s\n", esp_err_to_name(err));
            last_send = now;
            send_interval = pdMS_TO_TICKS(1000 + esp_random() % 1000);
        }
        if (now - last_print >= pdMS_TO_TICKS(3000)) {
            print_boards();
            last_print = now;
        }
    }
}

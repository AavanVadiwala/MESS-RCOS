#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include "mess_comm.h"
#include "mess_wifi.h"
#include "mess_commands.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_console.h"
#include "linenoise/linenoise.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_random.h"
#include "esp_app_desc.h"

/* Explicit byte layout: magic[8], version, type, nonce[4], ELF SHA256[32]. */
#define DISCOVERY_SIZE 46
#define DISCOVERY_REQUEST 1
#define DISCOVERY_REPLY 2
#define DISCOVERY_PROBES 10
static const uint8_t discovery_magic[8] = {'M','E','S','S','D','I','S','C'};

typedef struct { uint8_t mac[6]; uint8_t data[200]; int length; int rssi; } packet_t;
static QueueHandle_t inbox, history, completion;
static QueueHandle_t discovery_requests, discovery_results;
static SemaphoreHandle_t transmit_lock;
static bool ready, now_started;
static bool pending;
/* Accessed only while holding mess_commands_lock. */
static bool discovery_live;
static TaskHandle_t discovery_live_task;
static bool discovery_frame(const uint8_t *data, int length)
{
    return length == DISCOVERY_SIZE && !memcmp(data, discovery_magic, 8) &&
           data[8] == 1 && (data[9] == DISCOVERY_REQUEST || data[9] == DISCOVERY_REPLY) &&
           !memcmp(data + 14, esp_app_get_description()->app_elf_sha256, 32);
}
static void discovery_responder(void *argument)
{
    (void)argument;
    packet_t packet;
    const uint8_t broadcast[6] = {255,255,255,255,255,255};
    for (;;) {
        if (xQueueReceive(discovery_requests, &packet, portMAX_DELAY) != pdTRUE) continue;
        /* Spread replies in time so nearby boards do not all answer at once. */
        vTaskDelay(pdMS_TO_TICKS(20 + esp_random() % 180));
        packet.data[9] = DISCOVERY_REPLY;
        mess_comm_send(broadcast, packet.data, DISCOVERY_SIZE);
    }
}
static void print_mac(const uint8_t mac[6], FILE *out);
static void print_packet(const packet_t *packet, FILE *out)
{
    print_mac(packet->mac, out);
    fprintf(out, " (%d bytes, RSSI %d dBm): ", packet->length, packet->rssi);
    for (int i = 0; i < packet->length; ++i) {
        unsigned c = packet->data[i];
        if (c >= 32 && c <= 126) fputc(c, out); else fprintf(out, "\\x%02x", c);
    }
    fputc('\n', out);
}
static void receive_task(void *argument)
{
    (void)argument;
    packet_t packet, discarded;
    for (;;) {
        if (xQueueReceive(inbox, &packet, portMAX_DELAY) != pdTRUE) continue;
        /* Serialize printing with commands, never perform UART I/O in the radio callback. */
        mess_commands_lock();
        if (xQueueSend(history, &packet, 0) != pdTRUE) {
            xQueueReceive(history, &discarded, 0);
            xQueueSend(history, &packet, 0);
        }
        fputs("\n[ESP-NOW RX] ", stdout);
        print_packet(&packet, stdout);
        fflush(stdout);
        mess_commands_unlock();
    }
}
static void received(const esp_now_recv_info_t *info, const uint8_t *data, int length)
{
    if (!info || !data || length < 1 || length > 200) return;
    packet_t packet = {.length=length, .rssi=info->rx_ctrl ? info->rx_ctrl->rssi : 0};
    memcpy(packet.mac, info->src_addr, 6);
    memcpy(packet.data, data, length);
    if (length >= 8 && !memcmp(data, discovery_magic, 8)) {
        if (discovery_frame(data, length)) {
            xQueueSend(data[9] == DISCOVERY_REQUEST ? discovery_requests : discovery_results,
                       &packet, 0);
        }
        return;
    }
    /* Drop newest on full queue; never block the Wi-Fi task. */
    xQueueSend(inbox, &packet, 0);
}
static void sent(const esp_now_send_info_t *info, esp_now_send_status_t status)
{
    (void)info;
    xQueueOverwrite(completion, &status);
}
esp_err_t mess_comm_start(void)
{
    if (ready) return ESP_OK;
    esp_err_t err = mess_wifi_start();
    if (err != ESP_OK) return err;
    inbox = xQueueCreate(8, sizeof(packet_t));
    history = xQueueCreate(8, sizeof(packet_t));
    completion = xQueueCreate(1, sizeof(esp_now_send_status_t));
    discovery_requests = xQueueCreate(8, sizeof(packet_t));
    discovery_results = xQueueCreate(32, sizeof(packet_t));
    transmit_lock = xSemaphoreCreateMutex();
    if (!inbox || !history || !completion || !discovery_requests ||
        !discovery_results || !transmit_lock) { err=ESP_ERR_NO_MEM; goto fail; }
    err=esp_now_init(); if (err != ESP_OK) goto fail;
    now_started=true;
    err=esp_now_register_recv_cb(received); if (err != ESP_OK) goto fail;
    err=esp_now_register_send_cb(sent); if (err != ESP_OK) goto fail;
    TaskHandle_t responder = NULL;
    if (xTaskCreate(discovery_responder, "mess_discover", 3072, NULL, 3, &responder) != pdPASS) {
        err=ESP_ERR_NO_MEM; goto fail;
    }
    if (xTaskCreate(receive_task, "mess_receive", 3072, NULL, 3, NULL) != pdPASS) {
        vTaskDelete(responder);
        err=ESP_ERR_NO_MEM; goto fail;
    }
    ready=true;
    puts("ESP-NOW listening automatically on channel 1; long-range TX 250 Kbps. Enter comm to broadcast.");
    return ESP_OK;
fail:
    if (now_started) { esp_now_deinit(); now_started=false; }
    if (inbox) { vQueueDelete(inbox); inbox=NULL; }
    if (history) { vQueueDelete(history); history=NULL; }
    if (completion) { vQueueDelete(completion); completion=NULL; }
    if (discovery_requests) { vQueueDelete(discovery_requests); discovery_requests=NULL; }
    if (discovery_results) { vQueueDelete(discovery_results); discovery_results=NULL; }
    if (transmit_lock) { vSemaphoreDelete(transmit_lock); transmit_lock=NULL; }
    return err;
}
static esp_err_t send_locked(const uint8_t destination[6], const uint8_t *data, size_t length)
{
    if (!ready) return ESP_ERR_INVALID_STATE;
    if (!destination || !data || !length || length>200) return ESP_ERR_INVALID_ARG;
    esp_now_send_status_t status;
    if (pending) {
        if (xQueueReceive(completion, &status, 0)!=pdTRUE) return ESP_ERR_INVALID_STATE;
        pending=false;
    }
    if (!esp_now_is_peer_exist(destination)) {
        esp_now_peer_info_t peer={.channel=0, .ifidx=WIFI_IF_STA, .encrypt=false};
        memcpy(peer.peer_addr,destination,6);
        esp_err_t err=esp_now_add_peer(&peer);
        if (err!=ESP_OK) return err;
    }
    /* Enabling the LR protocol alone does not select a long-range transmit rate. */
    esp_now_rate_config_t rate = {
        .phymode = WIFI_PHY_MODE_LR,
        .rate = WIFI_PHY_RATE_LORA_250K,
    };
    esp_err_t rate_err = esp_now_set_peer_rate_config(destination, &rate);
    if (rate_err != ESP_OK) return rate_err;
    xQueueReset(completion);
    pending=true;
    esp_err_t err=esp_now_send(destination,data,length);
    if (err!=ESP_OK) { pending=false; return err; }
    if (xQueueReceive(completion,&status,pdMS_TO_TICKS(2000))!=pdTRUE) return ESP_ERR_TIMEOUT;
    pending=false;
    return status==ESP_NOW_SEND_SUCCESS ? ESP_OK : ESP_FAIL;
}
esp_err_t mess_comm_send(const uint8_t destination[6], const uint8_t *data, size_t length)
{
    if (!ready) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(transmit_lock, portMAX_DELAY);
    esp_err_t err = send_locked(destination, data, length);
    xSemaphoreGive(transmit_lock);
    return err;
}
static esp_err_t discover(FILE *out)
{
    uint8_t request[DISCOVERY_SIZE] = {0}, own_mac[6], seen[32][6];
    const uint8_t broadcast[6] = {255,255,255,255,255,255};
    memcpy(request, discovery_magic, 8);
    request[8] = 1;
    request[9] = DISCOVERY_REQUEST;
    memcpy(request + 14, esp_app_get_description()->app_elf_sha256, 32);
    esp_err_t err = esp_wifi_get_mac(WIFI_IF_STA, own_mac);
    if (err != ESP_OK) return err;
    xQueueReset(discovery_results);
    unsigned count = 0;
    unsigned replies[32] = {0};
    int last_rssi[32] = {0};
    bool truncated = false;
    fprintf(out, "Searching current channel: 10 probes, about 5 seconds; exact firmware build only...\n");
    fflush(out);
    for (unsigned round = 0; round < DISCOVERY_PROBES; ++round) {
        uint32_t nonce = esp_random();
        memcpy(request + 10, &nonce, 4);
        bool answered[32] = {0};
        err = mess_comm_send(broadcast, request, sizeof(request));
        if (err != ESP_OK) return err;
        TickType_t started = xTaskGetTickCount(), window = pdMS_TO_TICKS(500);
        for (;;) {
            TickType_t elapsed = xTaskGetTickCount() - started;
            if (elapsed >= window) break;
            packet_t packet;
            if (xQueueReceive(discovery_results, &packet, window - elapsed) != pdTRUE) break;
            if (memcmp(packet.data + 10, &nonce, 4) || !memcmp(packet.mac, own_mac, 6)) continue;
            unsigned i;
            for (i = 0; i < count; ++i) if (!memcmp(seen[i], packet.mac, 6)) break;
            if (i == count) {
                if (count == 32) { truncated = true; continue; }
                memcpy(seen[count++], packet.mac, 6);
            }
            if (!answered[i]) { ++replies[i]; answered[i] = true; }
            last_rssi[i] = packet.rssi;
        }
    }
    fprintf(out, "Found %u other matching board(s) in direct radio range.\n", count);
    for (unsigned i = 0; i < count; ++i) {
        fprintf(out, "  "); print_mac(seen[i], out);
        fprintf(out, "  distance: unavailable  radio: 250 Kbps (configured)  RSSI %d dBm  replies %u/%u (%u%%)\n",
                last_rssi[i], replies[i], DISCOVERY_PROBES,
                replies[i] * 100 / DISCOVERY_PROBES);
    }
    fprintf(out, "Reply percentage is a short round-trip sample, not guaranteed delivery or identity verification.\n");
    if (truncated) fprintf(out, "Result limit reached (32 boards).\n");
    if (!count) fprintf(out, "Check that other boards have this same image, are powered on, and use the same channel.\n");
    return ESP_OK;
}
static void discovery_live_worker(void *argument)
{
    (void)argument;
    for (;;) {
        mess_commands_lock();
        if (!discovery_live) {
            discovery_live_task = NULL;
            mess_commands_unlock();
            vTaskDelete(NULL);
            return;
        }
        fputs("\n[Live discovery]\n", stdout);
        esp_err_t err = discover(stdout);
        if (err != ESP_OK) printf("Discovery error: %s\n", esp_err_to_name(err));
        fflush(stdout);
        mess_commands_unlock();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
static bool parse_mac(const char *text,uint8_t mac[6])
{
    if (strlen(text)!=17) return false;
    for (int i=0;i<6;++i) {
        char hex[3]={text[i*3],text[i*3+1],0}; char *end;
        if (strspn(hex,"0123456789abcdefABCDEF")!=2 || (i<5 && text[i*3+2]!=':')) return false;
        mac[i]=(uint8_t)strtoul(hex,&end,16);
    }
    return true;
}
static void print_mac(const uint8_t mac[6], FILE *out)
{
    fprintf(out, "%02x:%02x:%02x:%02x:%02x:%02x",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
}
static void help(FILE *out)
{
    fprintf(out, "%s\n", "status | discover [live|stop] | channel N (1..11) | send MAC \"message\" | broadcast \"message\" | receive | exit");
    fprintf(out, "%s\n", "discover: fresh link sample; discover live: repeat on USB; discover stop: stop updates. Distance is unavailable; Kbps is configured rate.");
    fprintf(out, "%s\n", "All boards listen at boot on channel 1. Long-range 250 Kbps; messages <=200 bytes, unencrypted.");
    fprintf(out, "%s\n", "Incoming messages print automatically on USB/UART, in any console mode. receive drains the last 8 messages.");
}
int mess_comm_run(int argc, char **argv, FILE *out)
{
    mess_commands_lock();
    esp_err_t err = mess_comm_start();
    if (err != ESP_OK) {
        fprintf(out, "Comm startup failed: %s\n", esp_err_to_name(err));
        mess_commands_unlock(); return 1;
    }
    int n = argc - 1;
    char **args = argv ? argv + 1 : NULL;
    if (n <= 0 || (n == 1 && !strcmp(args[0], "help"))) help(out);
    else {
        err=ESP_OK;
        if (!strcmp(args[0],"status") && n==1) {
            uint8_t mac[6],channel; wifi_second_chan_t secondary;
            err=esp_wifi_get_mac(WIFI_IF_STA,mac);
            if (err==ESP_OK) err=esp_wifi_get_channel(&channel,&secondary);
            if (err==ESP_OK) { fprintf(out, "MAC "); print_mac(mac, out); fprintf(out, " channel %u; LR 250 Kbps; automatic USB receive; history %u\n",channel,(unsigned)uxQueueMessagesWaiting(history)); }
        } else if (!strcmp(args[0],"discover") && n==1) {
            err = discover(out);
        } else if (!strcmp(args[0],"discover") && n==2 && !strcmp(args[1],"live")) {
            discovery_live = true;
            if (!discovery_live_task &&
                xTaskCreate(discovery_live_worker, "mess_live", 4096, NULL, 2,
                            &discovery_live_task) != pdPASS) {
                discovery_live = false;
                err = ESP_ERR_NO_MEM;
            }
            if (err == ESP_OK) fprintf(out, "Live discovery enabled on USB/UART (about every 6 seconds). Use discover stop to stop.\n");
        } else if (!strcmp(args[0],"discover") && n==2 && !strcmp(args[1],"stop")) {
            discovery_live = false;
            fprintf(out, "Live discovery stopped.\n");
        } else if (!strcmp(args[0],"channel") && n==2) {
            char *end; long channel=strtol(args[1],&end,10);
            if (*end || end==args[1] || channel<1 || channel>11) err=ESP_ERR_INVALID_ARG;
            else {
                xSemaphoreTake(transmit_lock, portMAX_DELAY);
                err = pending ? ESP_ERR_INVALID_STATE : mess_wifi_set_channel(channel);
                xSemaphoreGive(transmit_lock);
                if (err==ESP_OK) fprintf(out, "Shared radio channel: %ld\n",channel);
                else if (err==ESP_ERR_INVALID_STATE)
                    fprintf(out, "Disconnect hotspot clients before changing channels; both ESP32s must use the same channel.\n");
            }
        } else if (!strcmp(args[0],"receive") && n==1) {
            packet_t packet; unsigned count=0;
            while (count < 8 && xQueueReceive(history,&packet,0)==pdTRUE) {
                print_packet(&packet, out); ++count;
            }
            if (!count) fprintf(out, "%s\n", "No queued messages.");
        } else if ((!strcmp(args[0],"send") && n==3) || (!strcmp(args[0],"broadcast") && n==2)) {
            uint8_t mac[6]; memset(mac,255,6);
            bool unicast=!strcmp(args[0],"send");
            if (unicast && (!parse_mac(args[1],mac) || (mac[0]&1))) err=ESP_ERR_INVALID_ARG;
            else {
                const char *message=args[unicast?2:1];
                err=mess_comm_send(mac,(const uint8_t *)message,strlen(message));
                if (err==ESP_OK) fprintf(out, "%s\n", "Long-range radio send completed; check listening MCUs for [ESP-NOW RX]. Broadcast has no delivery acknowledgement.");
            }
        } else help(out);
        if (err!=ESP_OK) fprintf(out, "Comm error: %s\n",esp_err_to_name(err));
    }
    mess_commands_unlock();
    return err == ESP_OK ? 0 : 1;
}
int mess_comm_console(int argc, char **argv)
{
    (void)argc; (void)argv;
    if (mess_comm_run(0, NULL, stdout)) return 1;
    for (;;) {
        char *line = linenoise("comm> ");
        if (!line) break;
        char *args[8] = {"comm"};
        int n = esp_console_split_argv(line, args + 1, 7);
        if (n && !strcmp(args[1], "exit")) { linenoiseFree(line); break; }
        if (n) mess_comm_run(n + 1, args, stdout);
        linenoiseFree(line);
    }
    return 0;
}

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

typedef struct { uint8_t mac[6]; uint8_t data[200]; int length; } packet_t;
static QueueHandle_t inbox, completion;
static bool ready, now_started;
static bool pending;
static void received(const esp_now_recv_info_t *info, const uint8_t *data, int length)
{
    if (!info || !data || length < 1 || length > 200) return;
    packet_t packet = {.length=length};
    memcpy(packet.mac, info->src_addr, 6);
    memcpy(packet.data, data, length);
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
    completion = xQueueCreate(1, sizeof(esp_now_send_status_t));
    if (!inbox || !completion) { err=ESP_ERR_NO_MEM; goto fail; }
    err=esp_now_init(); if (err != ESP_OK) goto fail;
    now_started=true;
    err=esp_now_register_recv_cb(received); if (err != ESP_OK) goto fail;
    err=esp_now_register_send_cb(sent); if (err != ESP_OK) goto fail;
    ready=true;
    return ESP_OK;
fail:
    if (now_started) { esp_now_deinit(); now_started=false; }
    if (inbox) { vQueueDelete(inbox); inbox=NULL; }
    if (completion) { vQueueDelete(completion); completion=NULL; }
    return err;
}
esp_err_t mess_comm_send(const uint8_t destination[6], const uint8_t *data, size_t length)
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
    xQueueReset(completion);
    pending=true;
    esp_err_t err=esp_now_send(destination,data,length);
    if (err!=ESP_OK) { pending=false; return err; }
    if (xQueueReceive(completion,&status,pdMS_TO_TICKS(2000))!=pdTRUE) return ESP_ERR_TIMEOUT;
    pending=false;
    return status==ESP_NOW_SEND_SUCCESS ? ESP_OK : ESP_FAIL;
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
    fprintf(out, "%s\n", "status | channel N (1..11) | send MAC \"message\" | broadcast \"message\" | receive | exit");
    fprintf(out, "%s\n", "Both boards: enter comm, choose same channel. Messages <=200 bytes, unencrypted.");
    fprintf(out, "%s\n", "receive drains 8-packet inbox; new packets drop when full. Radio stays on after exit.");
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
            if (err==ESP_OK) { fprintf(out, "MAC "); print_mac(mac, out); fprintf(out, " channel %u; queued %u\n",channel,(unsigned)uxQueueMessagesWaiting(inbox)); }
        } else if (!strcmp(args[0],"channel") && n==2) {
            char *end; long channel=strtol(args[1],&end,10);
            if (*end || end==args[1] || channel<1 || channel>11) err=ESP_ERR_INVALID_ARG;
            else if (pending) err=ESP_ERR_INVALID_STATE;
            else {
                err=mess_wifi_set_channel(channel);
                if (err==ESP_OK) fprintf(out, "Shared radio channel: %ld\n",channel);
                else if (err==ESP_ERR_INVALID_STATE)
                    fprintf(out, "Disconnect hotspot clients before changing channels; both ESP32s must use the same channel.\n");
            }
        } else if (!strcmp(args[0],"receive") && n==1) {
            packet_t packet; unsigned count=0;
            while (count < 8 && xQueueReceive(inbox,&packet,0)==pdTRUE) {
                print_mac(packet.mac, out); fprintf(out, " (%d bytes): ",packet.length);
                for (int i=0;i<packet.length;++i) {
                    unsigned c=packet.data[i];
                    if (c>=32 && c<=126) fputc(c, out); else fprintf(out, "\\x%02x",c);
                }
                fprintf(out, "%s\n", ""); ++count;
            }
            if (!count) fprintf(out, "%s\n", "No queued messages.");
        } else if ((!strcmp(args[0],"send") && n==3) || (!strcmp(args[0],"broadcast") && n==2)) {
            uint8_t mac[6]; memset(mac,255,6);
            bool unicast=!strcmp(args[0],"send");
            if (unicast && (!parse_mac(args[1],mac) || (mac[0]&1))) err=ESP_ERR_INVALID_ARG;
            else {
                const char *message=args[unicast?2:1];
                err=mess_comm_send(mac,(const uint8_t *)message,strlen(message));
                if (err==ESP_OK) fprintf(out, "%s\n", "Radio send completed; use receive on the other MCU to verify application receipt.");
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

/* Wi-Fi setup adapted from Espressif's CC0/Public Domain softAP example:
 * C:/test_mc/esp-idf/examples/wifi/getting_started/softAP/main/softap_example_main.c
 * Uses APSTA so ESP-NOW and laptop traffic share one radio. */
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "mess_wifi.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"

static bool ready;
static esp_netif_t *ap;
esp_err_t mess_wifi_start(void)
{
    if (ready) return ESP_OK;
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) return err;
    err = esp_netif_init();
    if (err != ESP_OK) return err;
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    ap = esp_netif_create_default_wifi_ap();
    if (!ap) return ESP_ERR_NO_MEM;
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init);
    if (err != ESP_OK) goto fail_netif;
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) goto fail_wifi;
    err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err != ESP_OK) goto fail_wifi;
    /* LR is for ESP-to-ESP traffic on STA. Keep the AP compatible with laptops. */
    err = esp_wifi_set_protocol(WIFI_IF_STA,
            WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N | WIFI_PROTOCOL_LR);
    if (err != ESP_OK) goto fail_wifi;
    uint8_t mac[6];
    err = esp_wifi_get_mac(WIFI_IF_AP, mac);
    if (err != ESP_OK) goto fail_wifi;
    wifi_config_t config = { .ap = {
        .password = "mess-demo-123", .channel = 1, .max_connection = 4,
        .authmode = WIFI_AUTH_WPA2_PSK,
    }};
    snprintf((char *)config.ap.ssid, sizeof(config.ap.ssid),
             "MESS-%02X%02X%02X", mac[3], mac[4], mac[5]);
    config.ap.ssid_len = strlen((char *)config.ap.ssid);
    err = esp_wifi_set_config(WIFI_IF_AP, &config);
    if (err != ESP_OK) goto fail_wifi;
    err = esp_wifi_start();
    if (err != ESP_OK) goto fail_wifi;
    err = esp_wifi_set_ps(WIFI_PS_NONE);
    if (err != ESP_OK) { esp_wifi_stop(); goto fail_wifi; }
    ready = true;
    return ESP_OK;
fail_wifi:
    esp_wifi_deinit();
fail_netif:
    esp_netif_destroy_default_wifi(ap);
    ap = NULL;
    return err;
}
esp_err_t mess_wifi_set_channel(unsigned channel)
{
    if (!ready) return ESP_ERR_INVALID_STATE;
    if (channel < 1 || channel > 11) return ESP_ERR_INVALID_ARG;
    uint8_t current_channel;
    wifi_second_chan_t secondary;
    esp_err_t err = esp_wifi_get_channel(&current_channel, &secondary);
    if (err != ESP_OK) return err;
    if (channel == current_channel) return ESP_OK;
    wifi_sta_list_t stations;
    err = esp_wifi_ap_get_sta_list(&stations);
    if (err != ESP_OK) return err;
    if (stations.num) {
        puts("Disconnect laptops before changing the shared radio channel.");
        return ESP_ERR_INVALID_STATE;
    }
    wifi_config_t config;
    err = esp_wifi_get_config(WIFI_IF_AP, &config);
    if (err != ESP_OK) return err;
    config.ap.channel = channel;
    return esp_wifi_set_config(WIFI_IF_AP, &config);
}
void mess_wifi_print_status(FILE *out)
{
    wifi_config_t config;
    wifi_sta_list_t stations;
    esp_netif_ip_info_t ip;
    uint8_t channel;
    wifi_second_chan_t secondary;
    if (!ready || esp_wifi_get_config(WIFI_IF_AP, &config) != ESP_OK ||
        esp_netif_get_ip_info(ap, &ip) != ESP_OK ||
        esp_wifi_ap_get_sta_list(&stations) != ESP_OK ||
        esp_wifi_get_channel(&channel, &secondary) != ESP_OK) {
        fprintf(out, "%s\n", "Wi-Fi status unavailable."); return;
    }
    fprintf(out, "Hotspot: %s | password: mess-demo-123 | channel: %u | laptops: %u\n",
           config.ap.ssid, channel, stations.num);
    fprintf(out, "Laptop browser: http://" IPSTR "/ | UDP broadcast port: 3333\n", IP2STR(&ip.ip));
}

void mess_wifi_status(void) { mess_wifi_print_status(stdout); }

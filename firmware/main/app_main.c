#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cmd_i2ctools.h"
#include "esp_app_desc.h"
#include "esp_console.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#define MAX_BOARDS 32
#define HEADER_SIZE 58
#define CHUNK_SIZE 192
#define MAX_OUTPUT 2048
#define MAX_CHUNKS ((MAX_OUTPUT + CHUNK_SIZE - 1) / CHUNK_SIZE)
#define COMMAND_SIZE 160
#define EXPIRE_MS 6000
#define POLL_WAIT_MS 12000
#define ANNOUNCEMENT 0
#define REQUEST 1
#define RESPONSE 2
#define RATE_REQUEST 3
#define RATE_ACK 4
/* Byte layout: magic[8], version, ELF hash[32], type, id[4], requester[6],
 * chunk index, chunk count, result[4], payload[0..192]. Max frame: 250 bytes. */
static const uint8_t magic[8] = {'M','E','S','S','P','O','L','L'};
static const uint8_t broadcast[6] = {255,255,255,255,255,255};
typedef struct {
    uint8_t mac[6]; int rssi;
    bool announcement; uint32_t sequence, session; unsigned tx_rate;
} observation_t;
typedef struct {
    bool active; observation_t observation; TickType_t last_seen;
    uint32_t session, last_sequence, delivery_mask;
    unsigned delivery_samples;
} board_t;
typedef struct { uint8_t mac[6]; int rssi; int length; uint8_t data[250]; } packet_t;
typedef struct {
    uint8_t mac[6]; int rssi; bool has_rssi;
    char *output; unsigned chunks, mask, length; int result;
    uint32_t delivery_mask; unsigned delivery_samples, tx_rate; bool confirmed;
} result_t;
static board_t boards[MAX_BOARDS];
static uint8_t own_mac[6];
static QueueHandle_t observations, requests, responses, completion;
static SemaphoreHandle_t board_lock, transmit_lock, report_lock;
static bool pending;
/* Protected by transmit_lock; this controls the broadcast peer used by all sends. */
static unsigned tx_rate = 250;

static uint32_t read_id(const uint8_t *frame)
{
    uint32_t id; memcpy(&id, frame + 42, sizeof(id)); return id;
}
static void make_header(uint8_t *frame, unsigned type, uint32_t id, const uint8_t requester[6])
{
    memset(frame, 0, HEADER_SIZE);
    memcpy(frame, magic, sizeof(magic));
    frame[8] = 2;
    memcpy(frame + 9, esp_app_get_description()->app_elf_sha256, 32);
    frame[41] = type;
    memcpy(frame + 42, &id, sizeof(id));
    if (requester) memcpy(frame + 46, requester, 6);
}
static bool command_valid(const char *line)
{
    char copy[COMMAND_SIZE], *argv[70];
    if (!line || strlen(line) >= sizeof(copy) || strchr(line, '\n') || strchr(line, '\r')) return false;
    strcpy(copy, line);
    int argc = esp_console_split_argv(copy, argv, 70);
    return argc > 0 && i2ctools_command_allowed(argv[0]);
}
static void received(const esp_now_recv_info_t *info, const uint8_t *data, int length)
{
    if (!info || !info->rx_ctrl || !data || length < HEADER_SIZE || length > 250 ||
        memcmp(data, magic, sizeof(magic)) || data[8] != 2 ||
        memcmp(data + 9, esp_app_get_description()->app_elf_sha256, 32) ||
        !memcmp(info->src_addr, own_mac, 6)) return;
    if (data[41] > RATE_ACK) return;
    unsigned advertised_rate = 0;
    if (data[41] == ANNOUNCEMENT || data[41] == RATE_REQUEST || data[41] == RATE_ACK) {
        memcpy(&advertised_rate, data + 54, 4);
        if (advertised_rate != 250 && advertised_rate != 500) return;
    }
    if (data[41] == RATE_REQUEST && (length != HEADER_SIZE || memcmp(data + 46, info->src_addr, 6))) return;
    if (data[41] == RATE_ACK && (length != HEADER_SIZE + 4 || memcmp(data + 46, own_mac, 6))) return;
    if (data[41] == ANNOUNCEMENT && length != HEADER_SIZE) return;
    if (data[41] == REQUEST && (length <= HEADER_SIZE || length > HEADER_SIZE + COMMAND_SIZE ||
        memcmp(data + 46, info->src_addr, 6) || data[length - 1] != 0 ||
        memchr(data + HEADER_SIZE, 0, length - HEADER_SIZE - 1))) return;
    if (data[41] == RESPONSE && (memcmp(data + 46, own_mac, 6) ||
        !data[53] || data[53] > MAX_CHUNKS || data[52] >= data[53] ||
        (data[52] + 1 < data[53] && length != HEADER_SIZE + CHUNK_SIZE) ||
        (data[53] == MAX_CHUNKS && data[52] + 1 == data[53] &&
         length - HEADER_SIZE > MAX_OUTPUT - (MAX_CHUNKS - 1) * CHUNK_SIZE))) return;
    observation_t observation = {.rssi = info->rx_ctrl->rssi};
    observation.announcement = data[41] == ANNOUNCEMENT;
    if (observation.announcement) {
        observation.tx_rate = advertised_rate;
        observation.sequence = read_id(data);
        memcpy(&observation.session, data + 46, sizeof(observation.session));
    }
    if (data[41] == RATE_ACK) observation.tx_rate = advertised_rate;
    memcpy(observation.mac, info->src_addr, 6);
    xQueueSend(observations, &observation, 0);
    if (data[41] != ANNOUNCEMENT) {
        packet_t packet = {.length = length, .rssi = observation.rssi};
        memcpy(packet.mac, info->src_addr, 6);
        memcpy(packet.data, data, length);
        xQueueSend(data[41] == REQUEST || data[41] == RATE_REQUEST ? requests : responses, &packet, 0);
    }
}
static void sent(const esp_now_send_info_t *info, esp_now_send_status_t status)
{
    (void)info; xQueueOverwrite(completion, &status);
}
static esp_err_t transmit(uint8_t *data, size_t length, bool *submitted)
{
    if (submitted) *submitted = false;
    xSemaphoreTake(transmit_lock, portMAX_DELAY);
    esp_now_send_status_t status;
    esp_err_t err = ESP_OK;
    if (pending) {
        if (xQueueReceive(completion, &status, 0) != pdTRUE) {
            xSemaphoreGive(transmit_lock); return ESP_ERR_INVALID_STATE;
        }
        pending = false;
    }
    xQueueReset(completion);
    if (data[41] == ANNOUNCEMENT) memcpy(data + 54, &tx_rate, 4);
    err = esp_now_send(broadcast, data, length);
    if (err == ESP_OK) {
        if (submitted) *submitted = true;
        pending = true;
        if (xQueueReceive(completion, &status, pdMS_TO_TICKS(2000)) != pdTRUE) err = ESP_ERR_TIMEOUT;
        else { pending = false; err = status == ESP_NOW_SEND_SUCCESS ? ESP_OK : ESP_FAIL; }
    }
    xSemaphoreGive(transmit_lock);
    return err;
}
static unsigned configured_rate(void)
{
    xSemaphoreTake(transmit_lock, portMAX_DELAY);
    unsigned rate = tx_rate;
    xSemaphoreGive(transmit_lock);
    return rate;
}
static esp_err_t change_rate(unsigned rate)
{
    if (rate != 250 && rate != 500) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(transmit_lock, portMAX_DELAY);
    esp_now_send_status_t status;
    if (pending) {
        if (xQueueReceive(completion, &status, 0) != pdTRUE) {
            xSemaphoreGive(transmit_lock); return ESP_ERR_INVALID_STATE;
        }
        pending = false;
    }
    esp_now_rate_config_t config = {.phymode = WIFI_PHY_MODE_LR,
        .rate = rate == 500 ? WIFI_PHY_RATE_LORA_500K : WIFI_PHY_RATE_LORA_250K};
    esp_err_t err = esp_now_set_peer_rate_config(broadcast, &config);
    if (err == ESP_OK) tx_rate = rate;
    xSemaphoreGive(transmit_lock);
    return err;
}
typedef struct { char *data; size_t used; bool truncated; } capture_t;
static ssize_t capture_write(void *cookie, const char *data, size_t length)
{
    capture_t *capture = cookie;
    size_t available = MAX_OUTPUT - capture->used;
    size_t copied = length < available ? length : available;
    memcpy(capture->data + capture->used, data, copied);
    capture->used += copied;
    capture->data[capture->used] = 0;
    if (copied != length) capture->truncated = true;
    return length;
}
static char *execute(const char *command, int *result, size_t *length)
{
    /* Avoid the installed open_memstream's flush at an allocation boundary.
     * A bounded cookie stream never reallocates or writes beyond MAX_OUTPUT. */
    capture_t capture = {.data = calloc(MAX_OUTPUT + 1, 1)};
    if (!capture.data) { *result = ESP_ERR_NO_MEM; *length = 0; return NULL; }
    cookie_io_functions_t functions = {.write = capture_write};
    FILE *stream = fopencookie(&capture, "w", functions);
    if (!stream) { free(capture.data); *result = ESP_ERR_NO_MEM; *length = 0; return NULL; }
    *result = i2ctools_run_line(command, stream);
    if (fclose(stream) != 0) { free(capture.data); *length = 0; *result = ESP_FAIL; return NULL; }
    *length = capture.used;
    if (capture.truncated) {
        const char marker[] = "\n[output truncated]\n";
        memcpy(capture.data + MAX_OUTPUT - sizeof(marker) + 1, marker, sizeof(marker) - 1);
    }
    return capture.data;
}
static void responder(void *argument)
{
    (void)argument;
    struct { uint8_t mac[6]; uint32_t id; TickType_t time; bool used; } recent[8] = {0};
    unsigned next = 0;
    packet_t packet;
    for (;;) {
        if (xQueueReceive(requests, &packet, portMAX_DELAY) != pdTRUE) continue;
        uint32_t id = read_id(packet.data);
        TickType_t now = xTaskGetTickCount();
        bool duplicate = false;
        for (unsigned i = 0; i < 8; ++i)
            if (recent[i].used && recent[i].id == id && !memcmp(recent[i].mac, packet.mac, 6) &&
                now - recent[i].time < pdMS_TO_TICKS(60000)) duplicate = true;
        bool rate_request = packet.data[41] == RATE_REQUEST;
        if (duplicate || (!rate_request && !command_valid((char *)packet.data + HEADER_SIZE))) continue;
        memcpy(recent[next].mac, packet.mac, 6);
        recent[next].id = id; recent[next].time = now; recent[next].used = true;
        next = (next + 1) % 8;
        if (rate_request) {
            unsigned requested_rate; memcpy(&requested_rate, packet.data + 54, 4);
            int result = change_rate(requested_rate);
            vTaskDelay(pdMS_TO_TICKS(20 + esp_random() % 400));
            uint8_t ack[HEADER_SIZE + 4]; make_header(ack, RATE_ACK, id, packet.mac);
            unsigned actual_rate = configured_rate(); memcpy(ack + 54, &actual_rate, 4);
            memcpy(ack + HEADER_SIZE, &result, 4);
            transmit(ack, sizeof(ack), NULL);
            continue;
        }
        size_t length = 0; int result;
        char *output = execute((char *)packet.data + HEADER_SIZE, &result, &length);
        if (!output) { output = strdup("Unable to capture I2C output.\n"); length = output ? strlen(output) : 0; }
        vTaskDelay(pdMS_TO_TICKS(20 + esp_random() % 400));
        unsigned chunks = length ? (length + CHUNK_SIZE - 1) / CHUNK_SIZE : 1;
        for (unsigned i = 0; i < chunks; ++i) {
            uint8_t frame[250];
            make_header(frame, RESPONSE, id, packet.mac);
            frame[52] = i; frame[53] = chunks;
            memcpy(frame + 54, &result, sizeof(int));
            size_t offset = i * CHUNK_SIZE, size = length - offset;
            if (size > CHUNK_SIZE) size = CHUNK_SIZE;
            if (size) memcpy(frame + HEADER_SIZE, output + offset, size);
            if (transmit(frame, HEADER_SIZE + size, NULL) != ESP_OK) break;
            vTaskDelay(pdMS_TO_TICKS(10 + esp_random() % 30));
        }
        free(output);
    }
}
static void expire_boards(TickType_t now)
{
    for (unsigned i = 0; i < MAX_BOARDS; ++i)
        if (boards[i].active && now - boards[i].last_seen >= pdMS_TO_TICKS(EXPIRE_MS)) boards[i].active = false;
}
/* Count unique announcements over the latest observed sequence span (max 32).
 * Duplicates do not increase delivery; late packets can fill gaps in the window.
 * A per-boot session resets the sample when the sender reboots. */
static void update_delivery(board_t *board, const observation_t *observation)
{
    if (!observation->announcement) return;
    if (!board->delivery_samples || board->session != observation->session) {
        board->session = observation->session;
        board->last_sequence = observation->sequence;
        board->delivery_mask = 1; board->delivery_samples = 1;
        return;
    }
    uint32_t forward = observation->sequence - board->last_sequence;
    if (forward && forward < 0x80000000U) {
        board->delivery_mask = forward >= 32 ? 1 : (board->delivery_mask << forward) | 1U;
        board->delivery_samples = forward >= 32 || board->delivery_samples + forward >= 32
            ? 32 : board->delivery_samples + forward;
        board->last_sequence = observation->sequence;
    } else {
        uint32_t behind = board->last_sequence - observation->sequence;
        if (behind < board->delivery_samples) board->delivery_mask |= 1U << behind;
    }
}
static void print_delivery(uint32_t mask, unsigned samples)
{
    if (!samples) { printf("N/A (no announcement sample)"); return; }
    unsigned delivered = __builtin_popcount(mask & (samples == 32 ? UINT32_MAX : (1U << samples) - 1));
    printf("%u%% (%u/%u announcements)", (delivered * 100 + samples / 2) / samples, delivered, samples);
}
static void record(const observation_t *observation, TickType_t now)
{
    unsigned free_slot = MAX_BOARDS;
    for (unsigned i = 0; i < MAX_BOARDS; ++i) {
        if (!boards[i].active) { if (free_slot == MAX_BOARDS) free_slot = i; }
        else if (!memcmp(boards[i].observation.mac, observation->mac, 6)) {
            unsigned old_rate = boards[i].observation.tx_rate;
            boards[i].observation = *observation;
            if (!observation->tx_rate) boards[i].observation.tx_rate = old_rate;
            boards[i].last_seen = now;
            update_delivery(&boards[i], observation); return;
        }
    }
    if (free_slot < MAX_BOARDS) {
        boards[free_slot] = (board_t){.active = true, .observation = *observation, .last_seen = now};
        update_delivery(&boards[free_slot], observation);
    }
}
static void print_mac(const uint8_t *mac)
{
    printf("%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}
static void print_result(unsigned number, const uint8_t *mac, bool has_rssi, int rssi, uint32_t delivery_mask, unsigned delivery_samples, unsigned rate, const char *output)
{
    printf("\nMCU %u:\nMCU MAC: ", number); print_mac(mac);
    if (has_rssi) printf("\nRSSI: %d dBm\n", rssi);
    else puts("\nRSSI: N/A (local MCU)");
    if (rate) printf("TX rate: %u Kbps (configured)\n", rate);
    else puts("TX rate: unavailable");
    printf("Announcement PDR: ");
    if (has_rssi) print_delivery(delivery_mask, delivery_samples);
    else printf("N/A (local MCU)");
    putchar('\n');
    printf("I2C output:\n%s", output && *output ? output : "OK\n");
    if (output && *output && output[strlen(output) - 1] != '\n') putchar('\n');
}
static int poll_command(int argc, char **argv)
{
    char command[COMMAND_SIZE] = {0};
    if (argc < 2 || !i2ctools_command_allowed(argv[1])) {
        puts("Usage: poll {i2cconfig|i2cdetect|i2cget|i2cset|i2cdump} [arguments]"); return 1;
    }
    for (int i = 1; i < argc; ++i) {
        size_t length = strlen(command), extra = strlen(argv[i]) + (i > 1);
        if (length + extra >= sizeof(command)) { puts("I2C command must be at most 159 bytes."); return 1; }
        if (i > 1) strcat(command, " ");
        strcat(command, argv[i]);
    }
    if (!command_valid(command)) return 1;
    result_t *results = calloc(MAX_BOARDS, sizeof(result_t));
    if (!results) { puts("Not enough memory for poll."); return 1; }
    xSemaphoreTake(report_lock, portMAX_DELAY);
    printf("\nOne-shot poll: %s\n", command);
    size_t local_length = 0; int local_result;
    char *local = execute(command, &local_result, &local_length);
    print_result(1, own_mac, false, 0, 0, 0, configured_rate(), local ? local : "Unable to capture I2C output.\n");
    free(local);
    unsigned count = 0;
    xSemaphoreTake(board_lock, portMAX_DELAY);
    expire_boards(xTaskGetTickCount());
    for (unsigned i = 0; i < MAX_BOARDS; ++i) if (boards[i].active) {
        memcpy(results[count].mac, boards[i].observation.mac, 6);
        results[count].rssi = boards[i].observation.rssi;
        results[count].tx_rate = boards[i].observation.tx_rate;
        results[count].delivery_mask = boards[i].delivery_mask;
        results[count].delivery_samples = boards[i].delivery_samples;
        results[count++].has_rssi = true;
    }
    xSemaphoreGive(board_lock);
    xQueueReset(responses);
    uint32_t id = esp_random();
    uint8_t request[HEADER_SIZE + COMMAND_SIZE];
    make_header(request, REQUEST, id, own_mac);
    memcpy(request + HEADER_SIZE, command, strlen(command) + 1);
    esp_err_t err = transmit(request, HEADER_SIZE + strlen(command) + 1, NULL);
    if (err == ESP_OK) {
        TickType_t started = xTaskGetTickCount(), window = pdMS_TO_TICKS(POLL_WAIT_MS);
        packet_t packet;
        for (;;) {
            TickType_t elapsed = xTaskGetTickCount() - started;
            if (elapsed >= window || xQueueReceive(responses, &packet, window - elapsed) != pdTRUE) break;
            if (packet.data[41] != RESPONSE || read_id(packet.data) != id) continue;
            unsigned index;
            for (index = 0; index < count; ++index) if (!memcmp(results[index].mac, packet.mac, 6)) break;
            if (index == count) {
                if (count == MAX_BOARDS) continue;
                memcpy(results[count++].mac, packet.mac, 6);
            }
            result_t *r = &results[index];
            unsigned chunk = packet.data[52], chunks = packet.data[53];
            int result; memcpy(&result, packet.data + 54, sizeof(int));
            if (!r->output) {
                r->output = calloc(MAX_OUTPUT + 1, 1);
                if (!r->output) continue;
                r->chunks = chunks; r->result = result;
            }
            if (r->chunks != chunks || r->result != result || (r->mask & (1U << chunk))) continue;
            unsigned size = packet.length - HEADER_SIZE;
            memcpy(r->output + chunk * CHUNK_SIZE, packet.data + HEADER_SIZE, size);
            r->mask |= 1U << chunk; r->rssi = packet.rssi; r->has_rssi = true;
            if (chunk + 1 == chunks) r->length = chunk * CHUNK_SIZE + size;
        }
    } else printf("Poll request send failed: %s. It will not be retried.\n", esp_err_to_name(err));
    for (unsigned i = 0; i < count; ++i) {
        result_t *r = &results[i];
        bool complete = r->output && r->mask == (1U << r->chunks) - 1;
        if (complete) r->output[r->length] = 0;
        xSemaphoreTake(board_lock, portMAX_DELAY);
        for (unsigned j = 0; j < MAX_BOARDS; ++j)
            if (boards[j].active && !memcmp(boards[j].observation.mac, r->mac, 6)) {
                r->tx_rate = boards[j].observation.tx_rate;
                r->delivery_mask = boards[j].delivery_mask;
                r->delivery_samples = boards[j].delivery_samples;
                break;
            }
        xSemaphoreGive(board_lock);
        print_result(i + 2, r->mac, r->has_rssi, r->rssi, r->delivery_mask, r->delivery_samples, r->tx_rate,
            complete ? r->output : r->output ? "Incomplete reply (packet loss); command may have executed.\n" :
            err != ESP_OK ? "Request transmission failed; command may have executed.\n" :
            "No reply within 12 seconds; command may have executed.\n");
        free(r->output);
    }
    if (!count) puts("\nNo remote MCU replied or was recently detected.");
    if (count == MAX_BOARDS) puts("Remote result capacity: 32 MCUs.");
    puts("\nPoll complete (one execution per responding MCU; no automatic retries).");
    fflush(stdout);
    xSemaphoreGive(report_lock);
    free(results);
    return err == ESP_OK && local_result == ESP_OK ? 0 : 1;
}
static int rate_command(int argc, char **argv)
{
    if (argc != 2 || (strcmp(argv[1], "250") && strcmp(argv[1], "500"))) {
        puts("Usage: rate 250 | rate 500 (Kbps; local and nearby matching MCUs)"); return 1;
    }
    unsigned desired = strtoul(argv[1], NULL, 10);
    result_t *results = calloc(MAX_BOARDS, sizeof(result_t));
    if (!results) { puts("Not enough memory for rate change."); return 1; }
    xSemaphoreTake(report_lock, portMAX_DELAY);
    unsigned count = 0;
    xSemaphoreTake(board_lock, portMAX_DELAY);
    expire_boards(xTaskGetTickCount());
    for (unsigned i = 0; i < MAX_BOARDS; ++i) if (boards[i].active) {
        memcpy(results[count].mac, boards[i].observation.mac, 6);
        results[count].rssi = boards[i].observation.rssi;
        results[count].tx_rate = boards[i].observation.tx_rate;
        results[count].delivery_mask = boards[i].delivery_mask;
        results[count].delivery_samples = boards[i].delivery_samples;
        ++count;
    }
    xSemaphoreGive(board_lock);
    esp_err_t err = change_rate(desired);
    if (err != ESP_OK) {
        printf("Local rate change failed: %s\n", esp_err_to_name(err));
        xSemaphoreGive(report_lock); free(results); return 1;
    }
    xQueueReset(responses);
    uint32_t id = esp_random();
    uint8_t request[HEADER_SIZE]; make_header(request, RATE_REQUEST, id, own_mac);
    memcpy(request + 54, &desired, 4);
    printf("\nChanging local and nearby matching MCUs to %u Kbps...\n", desired);
    err = transmit(request, sizeof(request), NULL);
    if (err == ESP_OK) {
        TickType_t start = xTaskGetTickCount(), window = pdMS_TO_TICKS(POLL_WAIT_MS);
        packet_t packet;
        for (;;) {
            TickType_t elapsed = xTaskGetTickCount() - start;
            if (elapsed >= window || xQueueReceive(responses, &packet, window - elapsed) != pdTRUE) break;
            if (packet.data[41] != RATE_ACK || read_id(packet.data) != id) continue;
            unsigned i;
            for (i = 0; i < count; ++i) if (!memcmp(results[i].mac, packet.mac, 6)) break;
            if (i == count) { if (count == MAX_BOARDS) continue; memcpy(results[count++].mac, packet.mac, 6); }
            memcpy(&results[i].result, packet.data + HEADER_SIZE, 4);
            memcpy(&results[i].tx_rate, packet.data + 54, 4);
            results[i].confirmed = true; results[i].rssi = packet.rssi;
        }
    }
    printf("Local MCU "); print_mac(own_mac); printf(" : TX rate %u Kbps | RSSI N/A | PDR N/A | confirmed\n", configured_rate());
    bool failed = err != ESP_OK;
    for (unsigned i = 0; i < count; ++i) {
        result_t *r = &results[i];
        xSemaphoreTake(board_lock, portMAX_DELAY);
        for (unsigned j = 0; j < MAX_BOARDS; ++j)
            if (boards[j].active && !memcmp(boards[j].observation.mac, r->mac, 6)) {
                r->delivery_mask = boards[j].delivery_mask;
                r->delivery_samples = boards[j].delivery_samples; break;
            }
        xSemaphoreGive(board_lock);
        printf("MCU "); print_mac(r->mac);
        printf(" : TX rate %u Kbps%s | RSSI %d dBm | Announcement PDR ", r->tx_rate,
            r->confirmed ? "" : " (last observed)", r->rssi);
        print_delivery(r->delivery_mask, r->delivery_samples);
        if (!r->confirmed) { puts(" | no confirmation; rate change may have executed"); failed = true; }
        else if (r->result != ESP_OK || r->tx_rate != desired) {
            printf(" | change failed: %s\n", esp_err_to_name(r->result)); failed = true;
        } else puts(" | confirmed");
    }
    if (!count) puts("No remote confirmations or recently detected MCUs.");
    if (err != ESP_OK) printf("Request send failed: %s\n", esp_err_to_name(err));
    puts("Rate command complete. Changes last until reboot; no automatic retries.");
    fflush(stdout); xSemaphoreGive(report_lock); free(results);
    return failed ? 1 : 0;
}
static void scanner(void *argument)
{
    (void)argument;
    uint8_t frame[HEADER_SIZE]; make_header(frame, ANNOUNCEMENT, 0, NULL);
    uint32_t sequence = 0, session = esp_random();
    memcpy(frame + 46, &session, sizeof(session));
    TickType_t now = xTaskGetTickCount(), last_send = now, last_print = now;
    TickType_t interval = pdMS_TO_TICKS(200 + esp_random() % 1000);
    for (;;) {
        observation_t observation;
        bool got = xQueueReceive(observations, &observation, pdMS_TO_TICKS(100)) == pdTRUE;
        now = xTaskGetTickCount();
        xSemaphoreTake(board_lock, portMAX_DELAY);
        expire_boards(now); if (got) record(&observation, now);
        xSemaphoreGive(board_lock);
        if (now - last_send >= interval) {
            uint32_t candidate = sequence + 1;
            memcpy(frame + 42, &candidate, sizeof(candidate));
            bool submitted;
            transmit(frame, sizeof(frame), &submitted);
            if (submitted) sequence = candidate;
            last_send = now; interval = pdMS_TO_TICKS(1000 + esp_random() % 1000);
        }
        if (now - last_print >= pdMS_TO_TICKS(3000) && xSemaphoreTake(report_lock, 0) == pdTRUE) {
            xSemaphoreTake(board_lock, portMAX_DELAY);
            unsigned count = 0;
            puts("\nNearby MCUs (same firmware; channel 1):");
            for (unsigned i = 0; i < MAX_BOARDS; ++i) if (boards[i].active) {
                const observation_t *o = &boards[i].observation;
                printf("MCU "); print_mac(o->mac);
                printf(" : TX rate %u Kbps (configured) | RSSI %d dBm | Announcement PDR ", o->tx_rate, o->rssi);
                print_delivery(boards[i].delivery_mask, boards[i].delivery_samples);
                putchar('\n'); ++count;
            }
            if (!count) puts("No matching MCU detected.");
            xSemaphoreGive(board_lock); fflush(stdout); xSemaphoreGive(report_lock); last_print = now;
        }
    }
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
    requests = xQueueCreate(8, sizeof(packet_t));
    responses = xQueueCreate(64, sizeof(packet_t));
    completion = xQueueCreate(1, sizeof(esp_now_send_status_t));
    board_lock = xSemaphoreCreateMutex(); transmit_lock = xSemaphoreCreateMutex(); report_lock = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(observations && requests && responses && completion && board_lock && transmit_lock && report_lock ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(received));
    ESP_ERROR_CHECK(esp_now_register_send_cb(sent));
    esp_now_peer_info_t peer = {.channel = 1, .ifidx = WIFI_IF_STA, .encrypt = false};
    memcpy(peer.peer_addr, broadcast, 6);
    ESP_ERROR_CHECK(esp_now_add_peer(&peer));
    esp_now_rate_config_t rate = {.phymode = WIFI_PHY_MODE_LR, .rate = WIFI_PHY_RATE_LORA_250K};
    ESP_ERROR_CHECK(esp_now_set_peer_rate_config(broadcast, &rate));
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "mess>"; repl_config.max_cmdline_length = 192; repl_config.max_cmdline_args = 70;
    repl_config.task_stack_size = 8192;
    const esp_console_dev_uart_config_t uart = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart, &repl_config, &repl));
    register_i2ctools();
    esp_err_t i2c_error = i2ctools_run_line("i2cconfig --sda 21 --scl 22 --freq 100000", stdout);
    if (i2c_error != ESP_OK) printf("Default I2C initialization failed: %s\n", esp_err_to_name(i2c_error));
    const esp_console_cmd_t poll = {.command = "poll", .help = "Run an I2C command once on this MCU and nearby matching MCUs", .func = poll_command};
    ESP_ERROR_CHECK(esp_console_cmd_register(&poll));
    const esp_console_cmd_t rate_cmd = {.command = "rate", .help = "Set 250 or 500 Kbps on local and nearby matching MCUs", .func = rate_command};
    ESP_ERROR_CHECK(esp_console_cmd_register(&rate_cmd));
    /* UART REPL initialization already registers help. */
    ESP_ERROR_CHECK(xTaskCreate(responder, "poll_responder", 8192, NULL, 3, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreate(scanner, "mcu_scanner", 4096, NULL, 2, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    puts("Enter poll i2cdetect for a one-shot report. I2C defaults: SDA21, SCL22, 100 kHz.");
    puts("Use rate 250 or rate 500 to change transmission rate across nearby MCUs.");
    puts("Flash the same built image onto all boards. Remote I2C commands are unencrypted.");
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}

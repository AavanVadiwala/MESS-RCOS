/* HTTP setup follows Espressif's CC0/Public Domain simple HTTP server example;
 * socket sends follow examples/protocols/sockets/udp_client in C:/test_mc/esp-idf.
 * Console, browser viewer, and message history are MESS-specific. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include "mess_laptop.h"
#include "mess_wifi.h"
#include "mess_i2ctools.h"
#include "mess_comm.h"
#include "mess_commands.h"
#include "esp_console.h"
#include "esp_http_server.h"
#include "lwip/sockets.h"
#include "linenoise/linenoise.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define HISTORY 16
#define MAX_MESSAGE 480
typedef struct { unsigned seq; char text[MAX_MESSAGE + 1]; } message_t;
static message_t messages[HISTORY];
static unsigned sequence;
static SemaphoreHandle_t lock;
static httpd_handle_t server;
static int udp = -1;
static int broadcast_line(const char *text, FILE *out);
static const char page[] =
    "<!doctype html><html lang=\"en\"><meta charset=\"utf-8\">\n"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>MESS terminal</title>\n"
    "<style>body{font:17px system-ui;max-width:950px;margin:30px auto;padding:20px;background:#101b27;color:#eef5ff}pre{white-space:pre-wrap;overflow-wrap:anywhere;padding:18px;background:#172839;border:1px solid #35495e;border-radius:8px}#terminal{height:340px;overflow:auto;font:15px/1.5 monospace}form{display:flex;gap:10px;align-items:center}input{flex:1;min-width:0;background:#172839;color:#eef5ff;border:1px solid #526980;padding:12px;font:16px monospace}button{padding:12px;background:#bde5ff;color:#101b27;border:0;border-radius:5px}button:disabled{opacity:.5}h2{font-size:18px}</style>\n"
    "<h1>MESS terminal</h1><p>Run commands on the MCU. Enter <code>i2ctools</code>, <code>comm</code>, or <code>wifi</code>. Type <code>help</code> for commands and <code>exit</code> to return.</p>\n"
    "<p id=\"state\">Connecting...</p><pre id=\"terminal\" role=\"log\" aria-live=\"polite\">Type help to get started.\n"
    "</pre><form id=\"command-form\"><label id=\"prompt\" for=\"command\">mess&gt;</label><input id=\"command\" aria-label=\"MCU command\" autocomplete=\"off\" autocapitalize=\"off\" spellcheck=\"false\" placeholder=\"i2ctools scan\"><button id=\"send\" type=\"submit\">Send</button></form>\n"
    "<p><button id=\"clear\" type=\"button\">Clear display</button></p><h2>MCU broadcasts</h2><pre id=\"log\" aria-live=\"polite\"></pre>\n"
    "<script>\n"
    "let seen=0,mode='',busy=false;\n"
    "const log=document.getElementById('log'),state=document.getElementById('state');\n"
    "const terminal=document.getElementById('terminal'),input=document.getElementById('command');\n"
    "const prompt=document.getElementById('prompt'),send=document.getElementById('send');\n"
    "function append(text){terminal.textContent+=text;if(terminal.textContent.length>64000)terminal.textContent=terminal.textContent.slice(-48000);terminal.scrollTop=terminal.scrollHeight;}\n"
    "function setMode(next){mode=next;prompt.textContent=(mode||'mess')+'>';}\n"
    "document.getElementById('clear').onclick=()=>{log.textContent='';terminal.textContent='';};\n"
    "document.getElementById('command-form').onsubmit=async event=>{\n"
    " event.preventDefault();if(busy)return;const raw=input.value,cmd=raw.trim();if(!cmd)return;\n"
    " append((mode||'mess')+'> '+raw+'\\n');input.value='';\n"
    " if(cmd==='exit'){setMode('');append('Returned to mess>.\\n');input.focus();return;}\n"
    " let command=raw,nextMode=mode;\n"
    " if(['i2ctools','comm','wifi'].includes(cmd)){nextMode=cmd;command=cmd+' help';}\n"
    " else if(/^(i2ctools|i2c|comm|wifi)\\s/.test(cmd))command=cmd;\n"
    " else if(mode==='wifi')command='wifi '+(['help','status'].includes(cmd)?cmd:'broadcast '+raw);\n"
    " else if(mode)command=mode+' '+raw;\n"
    " if(new TextEncoder().encode(command).length>511){append('Command too long; maximum 511 bytes including mode.\\n');input.focus();return;}\n"
    " busy=true;send.disabled=true;input.disabled=true;\n"
    " try{const r=await fetch('/command',{method:'POST',headers:{'Content-Type':'text/plain;charset=UTF-8'},body:command});\n"
    " const output=await r.text();append(output+(output&&!output.endsWith('\\n')?'\\n':''));\n"
    " if(!r.ok)append('Request failed ('+r.status+').\\n');else if(r.headers.get('X-MESS-Result')==='0')setMode(nextMode);\n"
    " }catch(e){append('Connection lost; command may have run. Check the MCU before retrying.\\n');}\n"
    " finally{busy=false;send.disabled=false;input.disabled=false;input.focus();}\n"
    "};\n"
    "async function poll(){try{if(busy){setTimeout(poll,500);return;}const r=await fetch('/messages',{cache:'no-store'});\n"
    "if(!r.ok)throw Error();const raw=await r.text();\n"
    "const rows=raw.trim().split('\\n').filter(Boolean).map(line=>{\n"
    "const [seq,hex]=line.split('\\t');const bytes=Uint8Array.from(hex.match(/../g)||[],h=>parseInt(h,16));\n"
    "return {seq:Number(seq),text:new TextDecoder().decode(bytes)};});\n"
    "if(rows.length&&rows[rows.length-1].seq<seen)seen=0;\n"
    "let skipped=false;for(const m of rows){if(m.seq>seen){\n"
    "if(seen&&m.seq>seen+1)skipped=true;log.textContent+=m.text+'\\n';seen=m.seq;}}\n"
    "if(log.textContent.length>64000)log.textContent=log.textContent.slice(-48000);\n"
    "state.textContent=skipped?'Connected; some older broadcasts were missed.':'Connected';\n"
    "}catch(e){state.textContent='Disconnected; reconnecting...';}setTimeout(poll,500);}poll();\n"
    "</script></html>\n";

static esp_err_t root_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, page, HTTPD_RESP_USE_STRLEN);
}
static esp_err_t messages_get(httpd_req_t *req)
{
    message_t *snapshot = malloc(sizeof(messages));
    if (!snapshot) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No memory");
    xSemaphoreTake(lock, portMAX_DELAY);
    unsigned first = sequence > HISTORY ? sequence - HISTORY : 0;
    unsigned count = sequence - first;
    for (unsigned i = 0; i < count; ++i) snapshot[i] = messages[(first + i) % HISTORY];
    xSemaphoreGive(lock);
    /* Encode bytes as hex so quotes, markup, tabs and UTF-8 survive unchanged. */
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t err = ESP_OK;
    const char hex[] = "0123456789abcdef";
    char row[MAX_MESSAGE * 2 + 32];
    for (unsigned i = 0; i < count; ++i) {
        int pos = snprintf(row, sizeof(row), "%u\t", snapshot[i].seq);
        for (const unsigned char *p = (unsigned char *)snapshot[i].text; *p; ++p) {
            row[pos++] = hex[*p >> 4]; row[pos++] = hex[*p & 15];
        }
        row[pos++] = '\n';
        err = httpd_resp_send_chunk(req, row, pos);
        if (err != ESP_OK) break;
    }
    free(snapshot);
    if (err == ESP_OK) err = httpd_resp_send_chunk(req, NULL, 0);
    return err;
}
/* Never enter a linenoise loop in the HTTP task or redirect global stdout.
 * These one-shot handlers use an explicit output stream shared with UART. */
static int run_command(char *line, FILE *out)
{
    if (!strcmp(line, "wifi") || !strcmp(line, "wifi help")) {
        fprintf(out, "status | broadcast TEXT | exit\n");
        mess_wifi_print_status(out);
        return 0;
    }
    if (!strcmp(line, "wifi status")) { mess_wifi_print_status(out); return 0; }
    if (!strncmp(line, "wifi broadcast ", 15)) return broadcast_line(line + 15, out);
    char *args[70];
    int count = esp_console_split_argv(line, args, 70);
    if (!count) return 0;
    if (!strcmp(args[0], "i2ctools") || !strcmp(args[0], "i2c"))
        return mess_i2ctools_run(count, args, out);
    if (!strcmp(args[0], "comm")) return mess_comm_run(count, args, out);
    if (count == 1 && (!strcmp(args[0], "help") || !strcmp(args[0], "exit"))) {
        fprintf(out, "i2ctools: scan, read, write, write-read\n"
                     "comm: status, channel, send, broadcast, receive\n"
                     "wifi: status, broadcast TEXT\n"
                     "Enter a mode name, or use a full command such as i2ctools scan.\n");
        return 0;
    }
    fprintf(out, "Unknown command. Type help for available modes.\n");
    return 1;
}
static esp_err_t command_post(httpd_req_t *req)
{
    /* Browser commands must originate from this page, not another website. */
    if (httpd_req_get_hdr_value_len(req, "Origin")) {
        char origin[96], host[64], expected[96];
        if (httpd_req_get_hdr_value_str(req, "Origin", origin, sizeof(origin)) != ESP_OK ||
            httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host)) != ESP_OK)
            return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Invalid origin");
        snprintf(expected, sizeof(expected), "http://%s", host);
        if (strcmp(origin, expected))
            return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Commands must come from this page");
    }
    if (req->content_len < 1 || req->content_len > 511)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Command must be 1..511 bytes");
    char line[512];
    int length = 0;
    while (length < req->content_len) {
        int got = httpd_req_recv(req, line + length, req->content_len - length);
        if (got <= 0) {
            if (got == HTTPD_SOCK_ERR_TIMEOUT)
                httpd_resp_send_err(req, HTTPD_408_REQ_TIMEOUT, "Command upload timed out");
            return ESP_FAIL;
        }
        length += got;
    }
    if (memchr(line, 0, length) || memchr(line, '\n', length) || memchr(line, '\r', length))
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Send one command line");
    line[length] = 0;
    char *output = NULL;
    size_t output_length = 0;
    FILE *stream = open_memstream(&output, &output_length);
    if (!stream) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No memory");
    int result = run_command(line, stream);
    int output_error = ferror(stream);
    if (fclose(stream) != 0 || output_error) {
        free(output);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Output capture failed");
    }
    char result_text[12];
    snprintf(result_text, sizeof(result_text), "%d", result);
    httpd_resp_set_hdr(req, "X-MESS-Result", result_text);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    esp_err_t err = httpd_resp_send(req, output ? output : "", output_length);
    free(output);
    return err;
}
static esp_err_t favicon_get(httpd_req_t *req)
{
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, NULL, 0);
}
esp_err_t mess_laptop_start(void)
{
    esp_err_t err = mess_wifi_start();
    if (err != ESP_OK) return err;
    if (server) return ESP_OK;
    if (!lock) lock = xSemaphoreCreateMutex();
    if (!lock) return ESP_ERR_NO_MEM;
    udp = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (udp < 0) return ESP_FAIL;
    int enabled = 1;
    if (setsockopt(udp, SOL_SOCKET, SO_BROADCAST, &enabled, sizeof(enabled)) < 0) {
        close(udp); udp = -1; return ESP_FAIL;
    }
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;
    err = httpd_start(&server, &config);
    if (err != ESP_OK) goto fail;
    const httpd_uri_t root = {.uri="/", .method=HTTP_GET, .handler=root_get};
    const httpd_uri_t history = {.uri="/messages", .method=HTTP_GET, .handler=messages_get};
    const httpd_uri_t command = {.uri="/command", .method=HTTP_POST, .handler=command_post};
    const httpd_uri_t favicon = {.uri="/favicon.ico", .method=HTTP_GET, .handler=favicon_get};
    err = httpd_register_uri_handler(server, &root);
    if (err == ESP_OK) err = httpd_register_uri_handler(server, &history);
    if (err == ESP_OK) err = httpd_register_uri_handler(server, &command);
    if (err == ESP_OK) err = httpd_register_uri_handler(server, &favicon);
    if (err == ESP_OK) return ESP_OK;
    httpd_stop(server);
fail:
    server = NULL;
    close(udp); udp = -1;
    return err;
}
static int broadcast_line(const char *text, FILE *out)
{
    size_t length = strlen(text);
    if (!length || length > MAX_MESSAGE) {
        fprintf(out, "Message must be 1..480 bytes.\n"); return 1;
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    message_t *m = &messages[sequence % HISTORY];
    m->seq = ++sequence;
    memcpy(m->text, text, length + 1);
    struct sockaddr_in destination = {
        .sin_family = AF_INET, .sin_port = htons(3333),
        .sin_addr.s_addr = inet_addr("192.168.4.255"),
    };
    int sent = sendto(udp, text, length, 0, (struct sockaddr *)&destination, sizeof(destination));
    int send_errno = errno;
    xSemaphoreGive(lock);
    if (sent != (int)length) {
        fprintf(out, "UDP send failed (errno %d); browser history saved.\n", send_errno); return 1;
    }
    fprintf(out, "Broadcast sent; check receiver to confirm receipt.\n");
    return 0;
}
static void help(void)
{
    puts("Every ordinary line broadcasts to laptops on UDP 3333 and the browser viewer.");
    puts("help | status | exit | broadcast TEXT (sends reserved words literally)");
    puts("Maximum 480 bytes per line; browser keeps the last 16 lines in MCU RAM.");
    mess_wifi_status();
}
int mess_laptop_console(int argc, char **argv)
{
    (void)argc; (void)argv;
    esp_err_t err = mess_laptop_start();
    if (err != ESP_OK) { printf("Laptop Wi-Fi startup failed: %s\n", esp_err_to_name(err)); return 1; }
    help();
    for (;;) {
        char *line = linenoise("wifi> ");
        if (!line) break;
        if (!strcmp(line, "exit")) { linenoiseFree(line); break; }
        if (!strcmp(line, "help")) help();
        else if (!strcmp(line, "status")) mess_wifi_status();
        else {
            const char *text = !strncmp(line, "broadcast ", 10) ? line + 10 : line;
            size_t length = strlen(text);
            if (length > MAX_MESSAGE) puts("Message too long; maximum 480 bytes.");
            else if (length) {
                broadcast_line(text, stdout);
            }
        }
        linenoiseFree(line);
    }
    return 0;
}

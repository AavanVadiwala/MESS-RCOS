#include <math.h>
#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "bme280.h"
#include "sgp30.h"
#include "wifi_sender.h"

/* Reads up to three I2C sensors sharing one bus, once a second:
 * BME280 (temperature, humidity, pressure), BH1750 (light) and SGP30 (TVOC, eCO2).
 * Any of them may be missing; the others keep working. */
static const char *TAG = "SENSORS";
#define SGP30_WARMUP_S 15 /* the SGP30 reports fixed 400 ppm / 0 ppb for its first 15 s */

static i2c_master_bus_handle_t bus;
static i2c_master_dev_handle_t bme_dev, bh_dev, sgp_dev;

static i2c_master_dev_handle_t add_device(uint16_t addr)
{
    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = 100000,
    };
    i2c_master_dev_handle_t dev = NULL;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &config, &dev));
    return dev;
}

/* ---- BME280 through Bosch's driver ---- */

static BME280_INTF_RET_TYPE read_regs(uint8_t reg, uint8_t *data, uint32_t len, void *ctx)
{
    return i2c_master_transmit_receive(*(i2c_master_dev_handle_t *)ctx,
        &reg, 1, data, len, 100) == ESP_OK ? 0 : -1;
}

static BME280_INTF_RET_TYPE write_regs(uint8_t reg, const uint8_t *data, uint32_t len, void *ctx)
{
    uint8_t buffer[33];
    if (len > sizeof(buffer) - 1) return -1;
    buffer[0] = reg;
    memcpy(buffer + 1, data, len);
    return i2c_master_transmit(*(i2c_master_dev_handle_t *)ctx,
        buffer, len + 1, 100) == ESP_OK ? 0 : -1;
}

static void delay_us(uint32_t us, void *ctx)
{
    (void)ctx;
    /* Round task delays up: Bosch requires at least the requested duration. */
    uint32_t tick_us = portTICK_PERIOD_MS * 1000U;
    if (us >= tick_us) vTaskDelay((us + tick_us - 1) / tick_us + 1);
    else esp_rom_delay_us(us);
}

static bool checked(const char *operation, int8_t result)
{
    if (result == BME280_OK) return true;
    ESP_LOGE(TAG, "BME280 %s failed (%d); no valid reading", operation, result);
    return false;
}

static struct bme280_dev bme;
static uint32_t bme_measurement_us;

static bool bme280_start(void)
{
    /* Probe both BME280 addresses and verify its chip ID before configuring it. */
    for (uint16_t addr = 0x76; addr <= 0x77 && !bme_dev; addr++) {
        if (i2c_master_probe(bus, addr, 100) != ESP_OK) continue;
        bme_dev = add_device(addr);
        uint8_t reg = 0xD0, id = 0;
        esp_err_t err = i2c_master_transmit_receive(bme_dev, &reg, 1, &id, 1, 100);
        if (err == ESP_OK && id == BME280_CHIP_ID) {
            ESP_LOGI(TAG, "BME280 found at 0x%02x (chip ID 0x%02x)", addr, id);
            break;
        }
        ESP_LOGW(TAG, "Device at 0x%02x is not a verified BME280 (ID 0x%02x, %s)", addr, id, esp_err_to_name(err));
        ESP_ERROR_CHECK(i2c_master_bus_rm_device(bme_dev));
        bme_dev = NULL;
    }
    if (!bme_dev) { ESP_LOGW(TAG, "No BME280 at 0x76/0x77: no temperature, humidity or pressure"); return false; }
    bme = (struct bme280_dev){.intf = BME280_I2C_INTF, .read = read_regs, .write = write_regs,
                              .delay_us = delay_us, .intf_ptr = &bme_dev};
    struct bme280_settings settings = {0};
    if (!checked("initialize", bme280_init(&bme)) || !checked("read settings", bme280_get_sensor_settings(&settings, &bme)))
        return false;
    settings.osr_t = BME280_OVERSAMPLING_1X;
    settings.osr_p = BME280_OVERSAMPLING_1X;
    settings.osr_h = BME280_OVERSAMPLING_1X;
    settings.filter = BME280_FILTER_COEFF_OFF;
    return checked("configure", bme280_set_sensor_settings(BME280_SEL_ALL_SETTINGS, &settings, &bme)) &&
           checked("measurement delay", bme280_cal_meas_delay(&bme_measurement_us, &settings));
}

static bool bme280_read(mess_reading_t *r)
{
    struct bme280_data data = {0};
    if (!checked("start measurement", bme280_set_sensor_mode(BME280_POWERMODE_FORCED, &bme))) return false;
    delay_us(bme_measurement_us + 2000, NULL);
    if (!checked("read measurement", bme280_get_sensor_data(BME280_ALL, &data, &bme))) return false;
    r->temperature = data.temperature;
    r->humidity = data.humidity;
    r->pressure = data.pressure / 100.0;
    return true;
}

/* ---- BH1750 light sensor: plain I2C commands ---- */

static bool bh1750_command(uint8_t command)
{
    return i2c_master_transmit(bh_dev, &command, 1, 100) == ESP_OK;
}

static bool bh1750_start(void)
{
    /* 0x23 with ADDR low or floating, 0x5C with ADDR high. */
    for (const uint16_t *addr = (const uint16_t[]){0x23, 0x5C, 0}; *addr && !bh_dev; addr++) {
        if (i2c_master_probe(bus, *addr, 100) != ESP_OK) continue;
        bh_dev = add_device(*addr);
        /* Power on, then continuous high-resolution mode (1 lx, a new value every 120-180 ms). */
        if (bh1750_command(0x01) && bh1750_command(0x10)) {
            ESP_LOGI(TAG, "BH1750 found at 0x%02x", *addr);
            vTaskDelay(pdMS_TO_TICKS(180) + 1);
            return true;
        }
        ESP_LOGW(TAG, "Device at 0x%02x did not accept BH1750 commands", *addr);
        ESP_ERROR_CHECK(i2c_master_bus_rm_device(bh_dev));
        bh_dev = NULL;
    }
    ESP_LOGW(TAG, "No BH1750 at 0x23/0x5C: no light reading");
    return false;
}

static bool bh1750_read(mess_reading_t *r)
{
    uint8_t raw[2];
    if (i2c_master_receive(bh_dev, raw, sizeof(raw), 100) != ESP_OK) {
        ESP_LOGE(TAG, "BH1750 read failed; no light reading");
        return false;
    }
    r->lux = ((raw[0] << 8) | raw[1]) / 1.2f; /* datasheet: counts / 1.2 = lux in H-resolution mode */
    return true;
}

/* ---- SGP30 gas sensor through the project's driver ---- */

static int sgp_write(void *ctx, const uint8_t *data, size_t len)
{
    (void)ctx;
    return i2c_master_transmit(sgp_dev, data, len, 100) == ESP_OK ? 0 : -1;
}

static int sgp_read(void *ctx, uint8_t *data, size_t len)
{
    (void)ctx;
    return i2c_master_receive(sgp_dev, data, len, 100) == ESP_OK ? 0 : -1;
}

static void sgp_sleep(unsigned ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms) + 1); /* +1 tick: the driver needs AT LEAST ms */
}

static sgp30_t sgp = {.write = sgp_write, .read = sgp_read, .sleep_ms = sgp_sleep};
static unsigned sgp_samples;

static bool sgp30_start(void)
{
    if (i2c_master_probe(bus, 0x58, 100) != ESP_OK) { ESP_LOGW(TAG, "No SGP30 at 0x58: no TVOC or eCO2"); return false; }
    sgp_dev = add_device(0x58);
    uint64_t serial = 0;
    uint16_t features = 0;
    int err = sgp30_identify(&sgp, &serial, &features);
    if (!err) err = sgp30_init(&sgp);
    if (err) {
        ESP_LOGE(TAG, "SGP30 at 0x58 did not respond correctly (%d); no TVOC or eCO2", err);
        ESP_ERROR_CHECK(i2c_master_bus_rm_device(sgp_dev));
        sgp_dev = NULL;
        return false;
    }
    ESP_LOGI(TAG, "SGP30 found at 0x58 (serial %012llx, features 0x%04x); warming up for %d s",
             (unsigned long long)serial, features, SGP30_WARMUP_S);
    return true;
}

/* Grams of water per cubic metre from temperature and relative humidity (Magnus formula). */
static float absolute_humidity(float celsius, float rh)
{
    return 216.7f * (rh / 100.0f * 6.112f * expf(17.62f * celsius / (243.12f + celsius)) / (273.15f + celsius));
}

static bool sgp30_read(mess_reading_t *r)
{
    /* Feed the BME280's humidity in so the gas estimates account for it; skip it when unknown. */
    if (isfinite(r->temperature) && isfinite(r->humidity))
        sgp30_set_humidity(&sgp, absolute_humidity(r->temperature, r->humidity));
    uint16_t eco2 = 0, tvoc = 0;
    int err = sgp30_measure(&sgp, &eco2, &tvoc);
    if (err) { ESP_LOGE(TAG, "SGP30 measure failed (%d); no gas reading", err); return false; }
    /* The SGP30 must be measured once a second from start-up; only report after warm-up. */
    if (++sgp_samples <= SGP30_WARMUP_S) return false;
    r->eco2 = eco2;
    r->tvoc = tvoc;
    return true;
}

/* ---- Main loop ---- */

/* Opens the I2C bus on SDA/SCL and reports whether any of our sensors answer there. */
static bool open_bus(int sda, int scl)
{
    const i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = sda,
        .scl_io_num = scl,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        /* Use the breakouts' proper pull-ups to 3.3 V. */
        .flags.enable_internal_pullup = false,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &bus));
    static const uint16_t known[] = {0x76, 0x77, 0x23, 0x5C, 0x58}; /* BME280, BH1750, SGP30 */
    for (size_t i = 0; i < sizeof(known) / sizeof(known[0]); i++)
        if (i2c_master_probe(bus, known[i], 50) == ESP_OK) return true;
    ESP_ERROR_CHECK(i2c_del_master_bus(bus));
    bus = NULL;
    return false;
}

/* Uses the configured pins, or else the first common ESP32 pin pair where a sensor answers,
 * so boards wired to 21/22 or 25/26 work without changing the configuration. */
static bool find_bus(int *sda, int *scl)
{
    const int pairs[][2] = {{CONFIG_MESS_BME_SDA, CONFIG_MESS_BME_SCL}, {21, 22}, {25, 26}};
    for (size_t i = 0; i < sizeof(pairs) / sizeof(pairs[0]); i++) {
        if (i && pairs[i][0] == CONFIG_MESS_BME_SDA && pairs[i][1] == CONFIG_MESS_BME_SCL) continue;
        if (!open_bus(pairs[i][0], pairs[i][1])) continue;
        *sda = pairs[i][0];
        *scl = pairs[i][1];
        if (i) ESP_LOGW(TAG, "No sensors on configured SDA GPIO%d / SCL GPIO%d; found them on GPIO%d / GPIO%d instead",
                        CONFIG_MESS_BME_SDA, CONFIG_MESS_BME_SCL, *sda, *scl);
        return true;
    }
    return false;
}

static void scan_bus(int sda, int scl)
{
    char found[128] = "";
    for (uint16_t addr = 0x08; addr < 0x78; addr++)
        if (i2c_master_probe(bus, addr, 50) == ESP_OK)
            snprintf(found + strlen(found), sizeof(found) - strlen(found), " 0x%02x", addr);
    ESP_LOGI(TAG, "I2C devices on SDA GPIO%d / SCL GPIO%d:%s", sda, scl, found);
}

void app_main(void)
{
    mess_wifi_start();
    int sda, scl;
    if (!find_bus(&sda, &scl)) {
        ESP_LOGE(TAG, "No sensors found on SDA/SCL GPIO%d/%d, 21/22 or 25/26. Check power, GND, SDA, SCL and pull-ups, then restart.",
                 CONFIG_MESS_BME_SDA, CONFIG_MESS_BME_SCL);
        return;
    }
    scan_bus(sda, scl);
    bool has_bme = bme280_start(), has_bh = bh1750_start(), has_sgp = sgp30_start();
    if (!has_bme && !has_bh && !has_sgp) {
        ESP_LOGE(TAG, "No sensors found. Check wiring and restart.");
        return;
    }
    TickType_t previous = xTaskGetTickCount();
    while (true) {
        mess_reading_t r = {NAN, NAN, NAN, NAN, NAN, NAN};
        if (has_bme && bme280_read(&r))
            printf("Temperature: %.2f C | Humidity: %.2f %%RH | Pressure: %.2f hPa\n", r.temperature, r.humidity, r.pressure);
        if (has_bh && bh1750_read(&r)) printf("Light: %.1f lx\n", r.lux);
        if (has_sgp) {
            if (sgp30_read(&r)) printf("TVOC: %.0f ppb | eCO2: %.0f ppm\n", r.tvoc, r.eco2);
            else if (sgp_samples <= SGP30_WARMUP_S) printf("SGP30 warming up (%u/%d s)\n", sgp_samples, SGP30_WARMUP_S);
        }
        if (isfinite(r.temperature) || isfinite(r.lux) || isfinite(r.tvoc)) mess_wifi_submit(&r);
        xTaskDelayUntil(&previous, pdMS_TO_TICKS(1000));
    }
}

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_log.h"

#include "driver/i2c_master.h"

#include "leds/led_manager.h"
#include "sonido/sound_player.h"
#include "sonido/sound_catalog.h"

// -----------------------------------------------------------------------------
// CONFIG TEST
// -----------------------------------------------------------------------------

#define TEST_I2C_PORT              I2C_NUM_0
#define TEST_I2C_SDA               GPIO_NUM_22
#define TEST_I2C_SCL               GPIO_NUM_23
#define TEST_I2C_FREQ_HZ           400000

// LSM6DSO32 suele usar 0x6A u 0x6B.
// Tu imu_manager usa 0x6B.
#define LSM6DSO32_ADDR_PRIMARY     0x6B
#define LSM6DSO32_ADDR_SECONDARY   0x6A

#define REG_WHO_AM_I              0x0F
#define WHO_AM_I_LSM6DSO32        0x6C

#define REG_CTRL1_XL              0x10
#define REG_CTRL2_G               0x11
#define REG_CTRL3_C               0x12
#define REG_OUTX_L_G              0x22
#define REG_OUTX_L_A              0x28

#define CTRL3_C_BDU               0x40
#define CTRL3_C_IF_INC            0x04

// Accel: 104 Hz, ±8g.
// Gyro:  104 Hz, ±500 dps.
#define CTRL1_XL_104HZ_8G         0x48
#define CTRL2_G_104HZ_500DPS      0x44

#define MAIN_LOOP_MS              20
#define IMU_LOG_PERIOD_MS         250
#define LED_TEST_PERIOD_MS        2000
#define SOUND_TEST_PERIOD_MS      3000

static const char *TAG = "HW_TEST";

static i2c_master_bus_handle_t i2c_bus = NULL;
static i2c_master_dev_handle_t imu_dev = NULL;

static bool imu_found = false;
static uint8_t imu_addr = 0;

// -----------------------------------------------------------------------------
// UTILS
// -----------------------------------------------------------------------------

static uint32_t now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static int16_t to_i16_le(uint8_t l, uint8_t h)
{
    return (int16_t)((h << 8) | l);
}

static void log_esp_err(const char *what, esp_err_t err)
{
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "%s: OK", what);
    } else {
        ESP_LOGE(TAG, "%s: ERROR %s", what, esp_err_to_name(err));
    }
}

// -----------------------------------------------------------------------------
// I2C / IMU BAJO NIVEL
// -----------------------------------------------------------------------------

static esp_err_t i2c_test_init(void)
{
    ESP_LOGI(TAG, "Inicializando I2C: SDA=%d SCL=%d freq=%d",
             TEST_I2C_SDA, TEST_I2C_SCL, TEST_I2C_FREQ_HZ);

    i2c_master_bus_config_t cfg = {
        .i2c_port = TEST_I2C_PORT,
        .sda_io_num = TEST_I2C_SDA,
        .scl_io_num = TEST_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    return i2c_new_master_bus(&cfg, &i2c_bus);
}

static void i2c_scan(void)
{
    ESP_LOGI(TAG, "========== ESCANEO I2C ==========");

    int found_count = 0;

    for (uint8_t addr = 1; addr < 127; addr++) {
        esp_err_t err = i2c_master_probe(
            i2c_bus,
            addr,
            pdMS_TO_TICKS(50)
        );

        if (err == ESP_OK) {
            ESP_LOGI(TAG, "I2C device encontrado en 0x%02X", addr);
            found_count++;
        }
    }

    if (found_count == 0) {
        ESP_LOGE(TAG, "No se ha encontrado ningun dispositivo I2C.");
        ESP_LOGE(TAG, "Revisa SDA/SCL, VCC, GND y soldaduras.");
    } else {
        ESP_LOGI(TAG, "Total dispositivos I2C encontrados: %d", found_count);
    }

    ESP_LOGI(TAG, "=================================");
}

static esp_err_t imu_add_device(uint8_t addr)
{
    if (imu_dev != NULL) {
        i2c_master_bus_rm_device(imu_dev);
        imu_dev = NULL;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = TEST_I2C_FREQ_HZ,
    };

    return i2c_master_bus_add_device(i2c_bus, &dev_cfg, &imu_dev);
}

static esp_err_t imu_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t data[2] = { reg, val };

    return i2c_master_transmit(
        imu_dev,
        data,
        sizeof(data),
        pdMS_TO_TICKS(100)
    );
}

static esp_err_t imu_read_reg(uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_transmit_receive(
        imu_dev,
        &reg,
        1,
        data,
        len,
        pdMS_TO_TICKS(100)
    );
}

static bool imu_try_address(uint8_t addr)
{
    ESP_LOGI(TAG, "Probando IMU en direccion 0x%02X", addr);

    esp_err_t err = i2c_master_probe(
        i2c_bus,
        addr,
        pdMS_TO_TICKS(100)
    );

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No responde nada en 0x%02X: %s",
                 addr, esp_err_to_name(err));
        return false;
    }

    err = imu_add_device(addr);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo añadir device 0x%02X: %s",
                 addr, esp_err_to_name(err));
        return false;
    }

    uint8_t who = 0;
    err = imu_read_reg(REG_WHO_AM_I, &who, 1);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo leer WHO_AM_I en 0x%02X: %s",
                 addr, esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "WHO_AM_I en 0x%02X = 0x%02X", addr, who);

    if (who != WHO_AM_I_LSM6DSO32) {
        ESP_LOGW(TAG, "WHO_AM_I inesperado. Esperado 0x%02X para LSM6DSO32.",
                 WHO_AM_I_LSM6DSO32);
        ESP_LOGW(TAG, "Puede haber otra IMU, direccion correcta pero chip distinto, o fallo de lectura.");
    }

    imu_addr = addr;
    imu_found = true;

    return true;
}

static void imu_configure(void)
{
    if (!imu_found) {
        ESP_LOGE(TAG, "No configuro IMU porque no se ha detectado.");
        return;
    }

    ESP_LOGI(TAG, "Configurando LSM6DSO32...");

    log_esp_err("CTRL3_C = BDU | IF_INC",
                imu_write_reg(REG_CTRL3_C, CTRL3_C_BDU | CTRL3_C_IF_INC));

    log_esp_err("CTRL1_XL = 104Hz / +/-8g",
                imu_write_reg(REG_CTRL1_XL, CTRL1_XL_104HZ_8G));

    log_esp_err("CTRL2_G = 104Hz / +/-500dps",
                imu_write_reg(REG_CTRL2_G, CTRL2_G_104HZ_500DPS));

    vTaskDelay(pdMS_TO_TICKS(100));

    uint8_t ctrl1 = 0;
    uint8_t ctrl2 = 0;
    uint8_t ctrl3 = 0;

    imu_read_reg(REG_CTRL1_XL, &ctrl1, 1);
    imu_read_reg(REG_CTRL2_G, &ctrl2, 1);
    imu_read_reg(REG_CTRL3_C, &ctrl3, 1);

    ESP_LOGI(TAG, "Registros leidos despues de configurar:");
    ESP_LOGI(TAG, "  CTRL1_XL = 0x%02X", ctrl1);
    ESP_LOGI(TAG, "  CTRL2_G  = 0x%02X", ctrl2);
    ESP_LOGI(TAG, "  CTRL3_C  = 0x%02X", ctrl3);
}

static void imu_init_test(void)
{
    esp_err_t err = i2c_test_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Fallo inicializando I2C: %s", esp_err_to_name(err));
        return;
    }

    i2c_scan();

    if (!imu_try_address(LSM6DSO32_ADDR_PRIMARY)) {
        imu_try_address(LSM6DSO32_ADDR_SECONDARY);
    }

    if (!imu_found) {
        ESP_LOGE(TAG, "IMU no detectada en 0x%02X ni 0x%02X.",
                 LSM6DSO32_ADDR_PRIMARY, LSM6DSO32_ADDR_SECONDARY);
        return;
    }

    ESP_LOGI(TAG, "IMU detectada en 0x%02X", imu_addr);
    imu_configure();
}

static void imu_log_sample(void)
{
    if (!imu_found) {
        ESP_LOGW(TAG, "IMU no encontrada: no hay muestras que leer.");
        return;
    }

    uint8_t raw_g[6] = {0};
    uint8_t raw_a[6] = {0};

    esp_err_t err_g = imu_read_reg(REG_OUTX_L_G, raw_g, sizeof(raw_g));
    esp_err_t err_a = imu_read_reg(REG_OUTX_L_A, raw_a, sizeof(raw_a));

    if (err_g != ESP_OK) {
        ESP_LOGE(TAG, "Error leyendo gyro: %s", esp_err_to_name(err_g));
        return;
    }

    if (err_a != ESP_OK) {
        ESP_LOGE(TAG, "Error leyendo accel: %s", esp_err_to_name(err_a));
        return;
    }

    int16_t gx = to_i16_le(raw_g[0], raw_g[1]);
    int16_t gy = to_i16_le(raw_g[2], raw_g[3]);
    int16_t gz = to_i16_le(raw_g[4], raw_g[5]);

    int16_t ax = to_i16_le(raw_a[0], raw_a[1]);
    int16_t ay = to_i16_le(raw_a[2], raw_a[3]);
    int16_t az = to_i16_le(raw_a[4], raw_a[5]);

    // LSM6DSO32:
    // Accel +/-8g  -> 0.244 mg/LSB aprox.
    // Gyro +/-500 -> 17.50 mdps/LSB aprox.
    float ax_g = ax * 0.000244f;
    float ay_g = ay * 0.000244f;
    float az_g = az * 0.000244f;

    float gx_dps = gx * 0.01750f;
    float gy_dps = gy * 0.01750f;
    float gz_dps = gz * 0.01750f;

    float accel_norm_g = sqrtf(ax_g * ax_g + ay_g * ay_g + az_g * az_g);

    ESP_LOGI(TAG,
             "IMU raw accel=[%6d %6d %6d] gyro=[%6d %6d %6d]",
             ax, ay, az, gx, gy, gz);

    ESP_LOGI(TAG,
             "IMU conv accel_g=[%+.3f %+.3f %+.3f] |norm|=%.3fg gyro_dps=[%+.2f %+.2f %+.2f]",
             ax_g, ay_g, az_g, accel_norm_g,
             gx_dps, gy_dps, gz_dps);

    if (accel_norm_g < 0.70f || accel_norm_g > 1.30f) {
        ESP_LOGW(TAG, "Aceleracion rara en reposo. En quieto deberia estar cerca de 1g.");
    }
}

// -----------------------------------------------------------------------------
// LED TEST
// -----------------------------------------------------------------------------

static void led_test_next(void)
{
    static int step = 0;

    switch (step) {
        case 0:
            ESP_LOGI(TAG, "LED TEST: rojo solido");
            led_manager_set_solid(LED_COLOR_RED);
            break;

        case 1:
            ESP_LOGI(TAG, "LED TEST: verde solido");
            led_manager_set_solid(LED_COLOR_GREEN);
            break;

        case 2:
            ESP_LOGI(TAG, "LED TEST: azul solido");
            led_manager_set_solid(LED_COLOR_BLUE);
            break;

        case 3:
            ESP_LOGI(TAG, "LED TEST: diagonal amarillo / morado");
            led_manager_set_diagonal_dual(LED_COLOR_YELLOW, LED_COLOR_PURPLE);
            break;

        case 4:
            ESP_LOGI(TAG, "LED TEST: fuego");
            led_manager_set_fire();
            break;

        case 5:
            ESP_LOGI(TAG, "LED TEST: agua");
            led_manager_set_water();
            break;

        case 6:
            ESP_LOGI(TAG, "LED TEST: electricidad");
            led_manager_set_electricity();
            break;

        case 7:
            ESP_LOGI(TAG, "LED TEST: rainbow");
            led_manager_set_rainbow();
            break;

        case 8:
            ESP_LOGI(TAG, "LED TEST: blink ON");
            led_manager_set_blink_enabled(true);
            break;

        case 9:
        default:
            ESP_LOGI(TAG, "LED TEST: blink OFF + apagado");
            led_manager_set_blink_enabled(false);
            led_manager_set_off();
            break;
    }

    step++;
    if (step > 9) {
        step = 0;
    }
}

// -----------------------------------------------------------------------------
// SOUND TEST
// -----------------------------------------------------------------------------

static void sound_test_next(void)
{
    static size_t index = 0;

    if (sound_table_count == 0) {
        ESP_LOGW(TAG, "sound_table vacia");
        return;
    }

    const char *name = sound_table[index].name;

    ESP_LOGI(TAG,
             "SOUND TEST: reproduciendo [%u/%u] '%s' len=%u sr=%u",
             (unsigned)(index + 1),
             (unsigned)sound_table_count,
             name,
             (unsigned)sound_table[index].len,
             (unsigned)sound_table[index].sample_rate);

    bool ok = sound_player_play(name);

    if (!ok) {
        ESP_LOGE(TAG, "sound_player_play('%s') devolvio false", name);
    }

    index++;
    if (index >= sound_table_count) {
        index = 0;
    }
}

// -----------------------------------------------------------------------------
// APP MAIN
// -----------------------------------------------------------------------------

void app_main(void)
{
    ESP_LOGI(TAG, "==========================================");
    ESP_LOGI(TAG, "TEST HARDWARE CUBO");
    ESP_LOGI(TAG, "IMU + LEDs + ALTAVOZ");
    ESP_LOGI(TAG, "==========================================");

    ESP_LOGI(TAG, "Inicializando LEDs...");
    esp_err_t led_err = led_manager_init();
    log_esp_err("led_manager_init", led_err);

    if (led_err == ESP_OK) {
        led_manager_set_master_brightness(80);
        led_manager_set_solid(LED_COLOR_WHITE);
    }

    ESP_LOGI(TAG, "Inicializando sonido...");
    sound_player_init();
    ESP_LOGI(TAG, "sound_player_init: OK");

    ESP_LOGI(TAG, "Inicializando test IMU...");
    imu_init_test();

    ESP_LOGI(TAG, "Arranca bucle de diagnostico.");
    ESP_LOGI(TAG, "Mueve el cubo: deberian cambiar los valores accel/gyro.");
    ESP_LOGI(TAG, "En reposo, |accel| deberia estar cerca de 1g.");
    ESP_LOGI(TAG, "Los LEDs iran cambiando solos y el altavoz ira probando sonidos.");

    uint32_t last_imu_log = 0;
    uint32_t last_led_test = 0;
    uint32_t last_sound_test = 0;

    // Lanza un primer test inmediato.
    led_test_next();
    sound_test_next();

    while (1) {
        uint32_t now = now_ms();

        if (now - last_imu_log >= IMU_LOG_PERIOD_MS) {
            last_imu_log = now;
            imu_log_sample();
        }

        if (now - last_led_test >= LED_TEST_PERIOD_MS) {
            last_led_test = now;
            led_test_next();
        }

        if (now - last_sound_test >= SOUND_TEST_PERIOD_MS) {
            last_sound_test = now;
            sound_test_next();
        }

        vTaskDelay(pdMS_TO_TICKS(MAIN_LOOP_MS));
    }
}
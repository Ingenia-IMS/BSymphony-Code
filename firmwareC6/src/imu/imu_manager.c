#include "imu/imu_manager.h"

#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_err.h"

#include <stdint.h>
#include <stdbool.h>

// -----------------------------------------------------------------------------
// CONFIG I2C / LSM6DSO32
// -----------------------------------------------------------------------------

#define I2C_PORT        I2C_NUM_0
#define SDA_GPIO        GPIO_NUM_22
#define SCL_GPIO        GPIO_NUM_23

#define IMU_ADDR_PRIMARY      0x6B
#define IMU_ADDR_SECONDARY    0x6A

#define REG_WHO_AM_I    0x0F
#define WHO_AM_I_VALUE  0x6C

#define REG_CTRL1_XL    0x10
#define REG_CTRL2_G     0x11
#define REG_CTRL3_C     0x12

#define REG_OUTX_L_G    0x22
#define REG_OUTX_L_A    0x28

// CTRL3_C
#define CTRL3_C_BDU     0x40
#define CTRL3_C_IF_INC  0x04

// Config elegida:
// Acelerómetro: 104 Hz, ±8 g
// Giroscopio:   104 Hz, ±500 dps
#define CTRL1_XL_104HZ_8G       0x48
#define CTRL2_G_104HZ_500DPS    0x44

// -----------------------------------------------------------------------------
// TASK
// -----------------------------------------------------------------------------

#define IMU_TASK_STACK_WORDS    4096
#define IMU_TASK_PRIORITY       4
#define IMU_PERIOD_MS           20

// -----------------------------------------------------------------------------
// DETECCIÓN
// -----------------------------------------------------------------------------

#define PICKUP_COOLDOWN_MS          5000

#define PICKUP_GYRO_MIN             5000
#define PICKUP_ACCEL_DELTA_MIN      6000000LL
#define PICKUP_REQUIRED_COUNT       3

#define SHAKE_GYRO_MIN              30000
#define SHAKE_ACCEL_DELTA_MIN       16000000LL

#define SHAKE_REQUIRED_PEAKS        3
#define SHAKE_WINDOW_MS             900
#define SHAKE_MIN_PEAK_GAP_MS       80

static const char *TAG = "IMU";

// -----------------------------------------------------------------------------
// VARIABLES INTERNAS
// -----------------------------------------------------------------------------

static i2c_master_bus_handle_t bus = NULL;
static i2c_master_dev_handle_t dev = NULL;

static uint8_t s_imu_addr = 0;
static bool s_imu_ok = false;

static TaskHandle_t imu_task_handle = NULL;

static imu_event_callback_t pickup_callback = NULL;
static imu_event_callback_t shake_callback = NULL;

static int16_t ax, ay, az;
static int16_t gx, gy, gz;

static int64_t accel_norm = 0;
static int64_t prev_accel_norm = 0;
static int64_t accel_delta = 0;
static int32_t gyro_activity = 0;

static bool first_sample = true;

static uint32_t last_pickup_ms = 0;
static int pickup_counter = 0;

static uint32_t shake_window_start_ms = 0;
static uint32_t last_shake_peak_ms = 0;
static int shake_peak_count = 0;

// -----------------------------------------------------------------------------
// UTILS
// -----------------------------------------------------------------------------

static int64_t iabs64(int64_t x)
{
    return (x < 0) ? -x : x;
}

static int32_t iabs32(int32_t x)
{
    return (x < 0) ? -x : x;
}

static int16_t to_i16_le(uint8_t l, uint8_t h)
{
    return (int16_t)((h << 8) | l);
}

static uint32_t now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

// -----------------------------------------------------------------------------
// I2C / LSM6DSO32
// -----------------------------------------------------------------------------

static esp_err_t imu_write(uint8_t reg, uint8_t val)
{
    if (dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t data[2] = { reg, val };

    return i2c_master_transmit(
        dev,
        data,
        sizeof(data),
        pdMS_TO_TICKS(100)
    );
}

static esp_err_t imu_read(uint8_t reg, uint8_t *data, int len)
{
    if (dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    return i2c_master_transmit_receive(
        dev,
        &reg,
        1,
        data,
        len,
        pdMS_TO_TICKS(100)
    );
}

static bool imu_read_u8(uint8_t reg, uint8_t *val)
{
    return imu_read(reg, val, 1) == ESP_OK;
}

static esp_err_t imu_add_device(uint8_t addr)
{
    if (dev != NULL) {
        i2c_master_bus_rm_device(dev);
        dev = NULL;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = 400000,
    };

    return i2c_master_bus_add_device(bus, &dev_cfg, &dev);
}

static bool imu_try_address(uint8_t addr)
{
    ESP_LOGI(TAG, "Probando IMU en direccion 0x%02X", addr);

    esp_err_t err = imu_add_device(addr);
    if (err != ESP_OK) {
        ESP_LOGE(
            TAG,
            "No se pudo anadir device 0x%02X: %s",
            addr,
            esp_err_to_name(err)
        );
        return false;
    }

    vTaskDelay(pdMS_TO_TICKS(20));

    uint8_t who = 0;
    if (!imu_read_u8(REG_WHO_AM_I, &who)) {
        ESP_LOGW(TAG, "No se pudo leer WHO_AM_I en 0x%02X", addr);
        return false;
    }

    ESP_LOGI(TAG, "WHO_AM_I en 0x%02X = 0x%02X", addr, who);

    if (who != WHO_AM_I_VALUE) {
        ESP_LOGW(
            TAG,
            "WHO_AM_I inesperado en 0x%02X. Esperado 0x%02X",
            addr,
            WHO_AM_I_VALUE
        );
        return false;
    }

    s_imu_addr = addr;
    ESP_LOGI(TAG, "IMU encontrada en 0x%02X", s_imu_addr);

    return true;
}

static esp_err_t i2c_init_internal(void)
{
    if (bus != NULL) {
        ESP_LOGW(TAG, "Bus I2C ya inicializado");
        return ESP_OK;
    }

    i2c_master_bus_config_t cfg = {
        .i2c_port = I2C_PORT,
        .sda_io_num = SDA_GPIO,
        .scl_io_num = SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t err = i2c_new_master_bus(&cfg, &bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error creando bus I2C: %s", esp_err_to_name(err));
        return err;
    }

    if (imu_try_address(IMU_ADDR_PRIMARY)) {
        return ESP_OK;
    }

    if (imu_try_address(IMU_ADDR_SECONDARY)) {
        return ESP_OK;
    }

    ESP_LOGE(
        TAG,
        "IMU no encontrada ni en 0x%02X ni en 0x%02X",
        IMU_ADDR_PRIMARY,
        IMU_ADDR_SECONDARY
    );

    return ESP_ERR_NOT_FOUND;
}

// -----------------------------------------------------------------------------
// INIT / CALLBACKS
// -----------------------------------------------------------------------------

void imu_init(void)
{
    s_imu_ok = false;

    esp_err_t err = i2c_init_internal();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo inicializar la IMU");
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(50));

    uint8_t who = 0;
    if (!imu_read_u8(REG_WHO_AM_I, &who)) {
        ESP_LOGE(TAG, "No se pudo leer WHO_AM_I despues de inicializar I2C");
        return;
    }

    ESP_LOGI(TAG, "WHO_AM_I final = 0x%02X", who);

    if (who != WHO_AM_I_VALUE) {
        ESP_LOGE(
            TAG,
            "IMU incorrecta. Esperado 0x%02X, recibido 0x%02X",
            WHO_AM_I_VALUE,
            who
        );
        return;
    }

    err = imu_write(REG_CTRL3_C, CTRL3_C_BDU | CTRL3_C_IF_INC);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error configurando CTRL3_C: %s", esp_err_to_name(err));
        return;
    }

    err = imu_write(REG_CTRL1_XL, CTRL1_XL_104HZ_8G);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error configurando CTRL1_XL: %s", esp_err_to_name(err));
        return;
    }

    err = imu_write(REG_CTRL2_G, CTRL2_G_104HZ_500DPS);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error configurando CTRL2_G: %s", esp_err_to_name(err));
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(100));

    // Reiniciamos estados internos de detección
    ax = ay = az = 0;
    gx = gy = gz = 0;

    accel_norm = 0;
    prev_accel_norm = 0;
    accel_delta = 0;
    gyro_activity = 0;

    first_sample = true;

    last_pickup_ms = 0;
    pickup_counter = 0;

    shake_window_start_ms = 0;
    last_shake_peak_ms = 0;
    shake_peak_count = 0;

    s_imu_ok = true;

    ESP_LOGI(TAG, "LSM6DSO32 inicializada en 0x%02X", s_imu_addr);
}

void imu_set_pickup_callback(imu_event_callback_t cb)
{
    pickup_callback = cb;
}

void imu_set_shake_callback(imu_event_callback_t cb)
{
    shake_callback = cb;
}

// -----------------------------------------------------------------------------
// LECTURA + DETECCIÓN
// -----------------------------------------------------------------------------

static bool imu_read_sample(void)
{
    if (!s_imu_ok || dev == NULL) {
        return false;
    }

    uint8_t raw_g[6];
    uint8_t raw_a[6];

    esp_err_t err = imu_read(REG_OUTX_L_G, raw_g, 6);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Error leyendo gyro: %s", esp_err_to_name(err));
        return false;
    }

    err = imu_read(REG_OUTX_L_A, raw_a, 6);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Error leyendo accel: %s", esp_err_to_name(err));
        return false;
    }

    gx = to_i16_le(raw_g[0], raw_g[1]);
    gy = to_i16_le(raw_g[2], raw_g[3]);
    gz = to_i16_le(raw_g[4], raw_g[5]);

    ax = to_i16_le(raw_a[0], raw_a[1]);
    ay = to_i16_le(raw_a[2], raw_a[3]);
    az = to_i16_le(raw_a[4], raw_a[5]);

    prev_accel_norm = accel_norm;

    accel_norm =
        (int64_t)ax * ax +
        (int64_t)ay * ay +
        (int64_t)az * az;

    accel_delta = iabs64(accel_norm - prev_accel_norm);
    gyro_activity = iabs32(gx) + iabs32(gy) + iabs32(gz);

    if (first_sample) {
        first_sample = false;
        prev_accel_norm = accel_norm;
        accel_delta = 0;
    }

    return true;
}

static void detect_pickup(uint32_t now, bool strong_shake_sample)
{
    bool pickup_motion =
        !strong_shake_sample &&
        (
            gyro_activity > PICKUP_GYRO_MIN ||
            accel_delta > PICKUP_ACCEL_DELTA_MIN
        );

    if (pickup_motion) {
        pickup_counter++;
    } else {
        pickup_counter = 0;
    }

    if (pickup_counter >= PICKUP_REQUIRED_COUNT &&
        now - last_pickup_ms >= PICKUP_COOLDOWN_MS) {

        ESP_LOGI(
            TAG,
            "DETECTADO COGER/MOVER -> sonido | gyro=%ld accel_delta=%lld",
            (long)gyro_activity,
            (long long)accel_delta
        );

        last_pickup_ms = now;
        pickup_counter = 0;

        if (pickup_callback != NULL) {
            pickup_callback();
        }
    }
}

static void detect_shake(uint32_t now, bool strong_shake_sample)
{
    if (!strong_shake_sample) {
        if (shake_window_start_ms != 0 &&
            now - shake_window_start_ms > SHAKE_WINDOW_MS) {
            shake_peak_count = 0;
            shake_window_start_ms = 0;
            last_shake_peak_ms = 0;
        }

        return;
    }

    if (shake_peak_count == 0) {
        shake_window_start_ms = now;
        last_shake_peak_ms = now;
        shake_peak_count = 1;
    } else {
        bool inside_window = (now - shake_window_start_ms) <= SHAKE_WINDOW_MS;
        bool enough_gap = (now - last_shake_peak_ms) >= SHAKE_MIN_PEAK_GAP_MS;

        if (!inside_window) {
            shake_window_start_ms = now;
            last_shake_peak_ms = now;
            shake_peak_count = 1;
        } else if (enough_gap) {
            last_shake_peak_ms = now;
            shake_peak_count++;
        }
    }

    if (shake_peak_count >= SHAKE_REQUIRED_PEAKS) {
        ESP_LOGI(
            TAG,
            "DETECTADO AGITADO VIGOROSO -> parpadeo | gyro=%ld accel_delta=%lld peaks=%d",
            (long)gyro_activity,
            (long long)accel_delta,
            shake_peak_count
        );

        shake_peak_count = 0;
        shake_window_start_ms = 0;
        last_shake_peak_ms = 0;

        if (shake_callback != NULL) {
            shake_callback();
        }
    }
}

// -----------------------------------------------------------------------------
// TASK
// -----------------------------------------------------------------------------

static void imu_task(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "Task IMU arrancada");

    while (1) {
        if (imu_read_sample()) {
            uint32_t now = now_ms();

            bool strong_shake_sample =
                gyro_activity > SHAKE_GYRO_MIN &&
                accel_delta > SHAKE_ACCEL_DELTA_MIN;

            detect_shake(now, strong_shake_sample);
            detect_pickup(now, strong_shake_sample);
        }

        vTaskDelay(pdMS_TO_TICKS(IMU_PERIOD_MS));
    }
}

void imu_start_task(void)
{
    if (imu_task_handle != NULL) {
        return;
    }

    if (!s_imu_ok) {
        ESP_LOGE(TAG, "No se arranca la task IMU porque la IMU no esta inicializada");
        return;
    }

    xTaskCreate(
        imu_task,
        "imu_task",
        IMU_TASK_STACK_WORDS,
        NULL,
        IMU_TASK_PRIORITY,
        &imu_task_handle
    );
}
#include "imu/imu_manager.h"

#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include <stdint.h>
#include <stdbool.h>

// -----------------------------------------------------------------------------
// CONFIG I2C / MPU
// -----------------------------------------------------------------------------

#define I2C_PORT        I2C_NUM_0
#define SDA_GPIO        22
#define SCL_GPIO        23
#define MPU_ADDR        0x68

#define REG_PWR_MGMT_1  0x6B
#define REG_ACCEL_XOUT  0x3B

// -----------------------------------------------------------------------------
// TASK
// -----------------------------------------------------------------------------

#define IMU_TASK_STACK_WORDS    4096
#define IMU_TASK_PRIORITY       4
#define IMU_PERIOD_MS           20

// -----------------------------------------------------------------------------
// DETECCIÓN
// -----------------------------------------------------------------------------

// Coger / mover suave -> sonido
#define PICKUP_COOLDOWN_MS          5000
#define PICKUP_GYRO_MIN             8000
#define PICKUP_ACCEL_DELTA_MIN      45000000
#define PICKUP_REQUIRED_COUNT       3

// Agitar vigorosamente -> parpadeo
/* Estos valores iban bien, un poco duro pero correcto
#define SHAKE_GYRO_MIN              70000
#define SHAKE_ACCEL_DELTA_MIN       250000000
*/
#define SHAKE_GYRO_MIN              65000
#define SHAKE_ACCEL_DELTA_MIN       230000000

#define SHAKE_REQUIRED_PEAKS        5
#define SHAKE_WINDOW_MS             900
#define SHAKE_MIN_PEAK_GAP_MS       80

static const char *TAG = "IMU";

// -----------------------------------------------------------------------------
// VARIABLES INTERNAS
// -----------------------------------------------------------------------------

static i2c_master_bus_handle_t bus;
static i2c_master_dev_handle_t dev;

static TaskHandle_t imu_task_handle = NULL;

static imu_event_callback_t pickup_callback = NULL;
static imu_event_callback_t shake_callback = NULL;

static int16_t ax, ay, az;
static int16_t gx, gy, gz;

static int32_t accel_norm = 0;
static int32_t prev_accel_norm = 0;
static int32_t accel_delta = 0;
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

static int32_t iabs32(int32_t x)
{
    return (x < 0) ? -x : x;
}

static int16_t to_i16(uint8_t h, uint8_t l)
{
    return (int16_t)((h << 8) | l);
}

static uint32_t now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

// -----------------------------------------------------------------------------
// I2C / MPU
// -----------------------------------------------------------------------------

static void i2c_init_internal(void)
{
    i2c_master_bus_config_t cfg = {
        .i2c_port = I2C_PORT,
        .sda_io_num = SDA_GPIO,
        .scl_io_num = SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .flags.enable_internal_pullup = true
    };

    i2c_new_master_bus(&cfg, &bus);

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = MPU_ADDR,
        .scl_speed_hz = 400000
    };

    i2c_master_bus_add_device(bus, &dev_cfg, &dev);
}

static void mpu_write(uint8_t reg, uint8_t val)
{
    uint8_t data[2] = {reg, val};
    i2c_master_transmit(dev, data, 2, -1);
}

static void mpu_read(uint8_t reg, uint8_t *data, int len)
{
    i2c_master_transmit_receive(dev, &reg, 1, data, len, -1);
}

// -----------------------------------------------------------------------------
// INIT / CALLBACKS
// -----------------------------------------------------------------------------

void imu_init(void)
{
    i2c_init_internal();

    mpu_write(REG_PWR_MGMT_1, 0x00);
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_LOGI(TAG, "IMU inicializada");
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

static void imu_read_sample(void)
{
    uint8_t raw[14];
    mpu_read(REG_ACCEL_XOUT, raw, 14);

    ax = to_i16(raw[0], raw[1]);
    ay = to_i16(raw[2], raw[3]);
    az = to_i16(raw[4], raw[5]);

    gx = to_i16(raw[8], raw[9]);
    gy = to_i16(raw[10], raw[11]);
    gz = to_i16(raw[12], raw[13]);

    prev_accel_norm = accel_norm;

    accel_norm =
        (int32_t)ax * ax +
        (int32_t)ay * ay +
        (int32_t)az * az;

    accel_delta = iabs32(accel_norm - prev_accel_norm);
    gyro_activity = iabs32(gx) + iabs32(gy) + iabs32(gz);

    if (first_sample) {
        first_sample = false;
        prev_accel_norm = accel_norm;
        accel_delta = 0;
    }
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
            "DETECTADO COGER/MOVER SUAVE -> sonido | gyro=%ld accel_delta=%ld",
            (long)gyro_activity,
            (long)accel_delta
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
        if (now - shake_window_start_ms > SHAKE_WINDOW_MS) {
            shake_peak_count = 0;
            shake_window_start_ms = 0;
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
            "DETECTADO AGITADO VIGOROSO -> parpadeo | gyro=%ld accel_delta=%ld peaks=%d",
            (long)gyro_activity,
            (long)accel_delta,
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
        imu_read_sample();

        uint32_t now = now_ms();

        bool strong_shake_sample =
            gyro_activity > SHAKE_GYRO_MIN &&
            accel_delta > SHAKE_ACCEL_DELTA_MIN;

        detect_shake(now, strong_shake_sample);
        detect_pickup(now, strong_shake_sample);

        vTaskDelay(pdMS_TO_TICKS(IMU_PERIOD_MS));
    }
}

void imu_start_task(void)
{
    if (imu_task_handle != NULL) {
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
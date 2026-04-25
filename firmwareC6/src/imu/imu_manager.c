#include "imu_manager.h"

#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdint.h>
#include <stdbool.h>

#define I2C_PORT        I2C_NUM_0
#define SDA_GPIO        22
#define SCL_GPIO        23
#define MPU_ADDR        0x68

#define REG_PWR_MGMT_1  0x6B
#define REG_ACCEL_XOUT  0x3B

#define SOUND_COOLDOWN_MS           5000

#define PICKUP_GYRO_MIN             9000
#define PICKUP_ACCEL_DELTA_MIN      70000000

#define SHAKE_GYRO_MIN              35000
#define SHAKE_REQUIRED_COUNT        6
#define SHAKE_RESET_GYRO_MAX        12000

static i2c_master_bus_handle_t bus;
static i2c_master_dev_handle_t dev;

static int16_t ax, ay, az;
static int16_t gx, gy, gz;

static int32_t accel_norm = 0;
static int32_t prev_accel_norm = 0;
static int32_t accel_delta = 0;
static int32_t gyro_activity = 0;

static bool sound_event = false;
static bool blink_event = false;

static uint32_t last_sound_ms = 0;
static int shake_counter = 0;
static bool first_sample = true;

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

static void i2c_init(void)
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

void imu_init(void)
{
    i2c_init();
    mpu_write(REG_PWR_MGMT_1, 0x00);
    vTaskDelay(pdMS_TO_TICKS(100));
}

void imu_update(void)
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
        return;
    }

    uint32_t now = now_ms();

    // -------------------------------------------------
    // COGER EL CUBO -> PARPADEO
    // Antes esto disparaba sonido.
    // -------------------------------------------------
    bool movimiento_claro =
        (gyro_activity > PICKUP_GYRO_MIN) ||
        (accel_delta > PICKUP_ACCEL_DELTA_MIN);

    if (movimiento_claro) {
        blink_event = true;
    }

    // -------------------------------------------------
    // AGITACIÓN VIGOROSA -> SONIDO
    // Antes esto disparaba parpadeo.
    // -------------------------------------------------
    if (gyro_activity > SHAKE_GYRO_MIN) {
        shake_counter++;
    } else if (gyro_activity < SHAKE_RESET_GYRO_MAX) {
        shake_counter = 0;
    }

    if (shake_counter >= SHAKE_REQUIRED_COUNT &&
        (now - last_sound_ms >= SOUND_COOLDOWN_MS)) {

        sound_event = true;
        last_sound_ms = now;
        shake_counter = 0;
    }
}

bool imu_take_sound_event(void)
{
    bool v = sound_event;
    sound_event = false;
    return v;
}

bool imu_take_blink_event(void)
{
    bool v = blink_event;
    blink_event = false;
    return v;
}
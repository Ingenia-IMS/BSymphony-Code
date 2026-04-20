#include "imu_manager.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdint.h>

// -----------------------------------------------------------------------------
// CONFIG
// -----------------------------------------------------------------------------

#define I2C_PORT        I2C_NUM_0
#define SDA_GPIO        22
#define SCL_GPIO        23
#define MPU_ADDR        0x68

#define REG_PWR_MGMT_1  0x6B
#define REG_ACCEL_XOUT  0x3B

// -----------------------------------------------------------------------------

static i2c_master_bus_handle_t bus;
static i2c_master_dev_handle_t dev;

// datos
static int16_t ax, ay, az;
static int16_t gx, gy, gz;

// métricas
static int32_t accel_norm = 0;
static int32_t prev_accel_norm = 0;
static int32_t accel_delta = 0;
static int32_t gyro_activity = 0;

// estado
static bool in_hand = false;
static bool on_table = true;

// eventos
static bool hit_event = false;
static bool shake_event = false;
static bool pickup_event = false;
static bool putdown_event = false;

// contadores
static int stable_counter = 0;
static int moving_counter = 0;
static int shake_counter = 0;
static int hit_cooldown = 0;

// -----------------------------------------------------------------------------
// UTIL
// -----------------------------------------------------------------------------

static int32_t iabs32(int32_t x)
{
    return (x < 0) ? -x : x;
}

static int16_t to_i16(uint8_t h, uint8_t l)
{
    return (int16_t)((h << 8) | l);
}

// -----------------------------------------------------------------------------
// I2C + MPU
// -----------------------------------------------------------------------------

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

// -----------------------------------------------------------------------------
// INIT
// -----------------------------------------------------------------------------

void imu_init(void)
{
    i2c_init();

    // despertar MPU
    mpu_write(REG_PWR_MGMT_1, 0x00);

    vTaskDelay(pdMS_TO_TICKS(100));
}

// -----------------------------------------------------------------------------
// UPDATE
// -----------------------------------------------------------------------------

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
    accel_norm = (int32_t)ax*ax + (int32_t)ay*ay + (int32_t)az*az;

    accel_delta = iabs32(accel_norm - prev_accel_norm);
    gyro_activity = iabs32(gx) + iabs32(gy) + iabs32(gz);

    // -------------------------
    // MESA (quieto)
    // -------------------------
    if (gyro_activity < 800 && accel_delta < 20000000) {
        stable_counter++;

        if (stable_counter > 20 && !on_table) {
            on_table = true;
            in_hand = false;
            putdown_event = true;
        }
    } else {
        stable_counter = 0;
    }

    // -------------------------
    // MANO (movimiento)
    // -------------------------
    if (gyro_activity > 4000 || accel_delta > 30000000) {
        moving_counter++;

        if (moving_counter > 5 && !in_hand) {
            in_hand = true;
            if (on_table) pickup_event = true;
            on_table = false;
        }
    } else {
        moving_counter = 0;
    }

    // -------------------------
    // GOLPE
    // -------------------------
    if (hit_cooldown > 0) hit_cooldown--;

    if (accel_delta > 80000000 && hit_cooldown == 0) {
        hit_event = true;
        hit_cooldown = 20;
    }

    // -------------------------
    // SHAKE
    // -------------------------
    if (gyro_activity > 8000) {
        shake_counter++;
    }

    if (shake_counter > 3) {
        shake_event = true;
        shake_counter = 0;
    }
}

// -----------------------------------------------------------------------------
// EVENTOS
// -----------------------------------------------------------------------------

bool imu_take_hit_event(void)
{
    bool v = hit_event;
    hit_event = false;
    return v;
}

bool imu_take_shake_event(void)
{
    bool v = shake_event;
    shake_event = false;
    return v;
}

bool imu_take_pickup_event(void)
{
    bool v = pickup_event;
    pickup_event = false;
    return v;
}

bool imu_take_putdown_event(void)
{
    bool v = putdown_event;
    putdown_event = false;
    return v;
}

// -----------------------------------------------------------------------------
// ESTADO
// -----------------------------------------------------------------------------

bool imu_is_in_hand(void)
{
    return in_hand;
}

bool imu_is_on_table(void)
{
    return on_table;
}
#include "ir_raw_test.h"

#include <string.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "../io.h"

static const char *TAG = "IR_RAW";

/*
 * Pines reales de tu prueba IR.
 */
#define IR_SEL0_GPIO        D8      // GPIO19
#define IR_SEL1_GPIO        D7      // GPIO17
#define IR_RX_GPIO          D10     // GPIO18
#define IR_TX_GPIO          D9      // GPIO20

/*
 * Si no ves hits nunca en receptor, prueba a cambiar esto a 1.
 */
#define IR_RX_ACTIVE_LEVEL  0

#define IR_TASK_STACK       4096
#define IR_TASK_PRIORITY    1

/*
 * Versión lenta anti-watchdog:
 * Nada de bucles largos con delay_us.
 * TX: deja el LED encendido varias decenas de ms.
 * RX: lee una muestra por tick.
 */
#define IR_TX_ON_MS         30
#define IR_TX_OFF_MS        30
#define IR_RX_TICK_MS       15
#define IR_SWEEP_STEP_MS    120
#define IR_LOG_PERIOD_MS    1000

typedef struct {
    bool running;
    ir_raw_mode_t mode;
    ir_raw_face_t face;
    ir_raw_stats_t stats;
} ir_raw_ctx_t;

static ir_raw_ctx_t s_ctx;
static SemaphoreHandle_t s_mutex;
static TaskHandle_t s_task;

static void ir_task(void *arg);
static void hw_init(void);
static void select_face(ir_raw_face_t face);
static void tx_on(void);
static void tx_off(void);
static bool rx_active(void);
static void reset_stats(void);
static void add_sample(ir_raw_face_t face, bool hit);
static void log_stats(void);

void ir_raw_test_init(void)
{
    memset(&s_ctx, 0, sizeof(s_ctx));
    s_ctx.mode = IR_RAW_MODE_RX_SWEEP;
    s_ctx.face = IR_RAW_FACE_0;

    s_mutex = xSemaphoreCreateMutex();

    hw_init();

    BaseType_t ok = xTaskCreate(
        ir_task,
        "ir_raw_test",
        IR_TASK_STACK,
        NULL,
        IR_TASK_PRIORITY,
        &s_task
    );

    if (ok != pdPASS) {
        ESP_LOGE(TAG, "No se pudo crear ir_raw_test");
    }
}

void ir_raw_test_start(ir_raw_mode_t mode, ir_raw_face_t face)
{
    if (face < IR_RAW_FACE_0 || face > IR_RAW_FACE_3) {
        face = IR_RAW_FACE_0;
    }

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        s_ctx.mode = mode;
        s_ctx.face = face;
        s_ctx.running = true;
        reset_stats();
        xSemaphoreGive(s_mutex);
    }

    ESP_LOGI(TAG, "START mode=%s face=%d", ir_raw_mode_name(mode), face);
}

bool ir_raw_test_get_stats(ir_raw_stats_t *out)
{
    if (out == NULL || s_mutex == NULL) {
        return false;
    }

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return false;
    }

    *out = s_ctx.stats;

    xSemaphoreGive(s_mutex);
    return true;
}

const char *ir_raw_mode_name(ir_raw_mode_t mode)
{
    switch (mode) {
        case IR_RAW_MODE_TX_FIXED: return "TX_FIXED";
        case IR_RAW_MODE_RX_FIXED: return "RX_FIXED";
        case IR_RAW_MODE_TX_SWEEP: return "TX_SWEEP";
        case IR_RAW_MODE_RX_SWEEP: return "RX_SWEEP";
        default: return "UNKNOWN";
    }
}

static void ir_task(void *arg)
{
    (void)arg;

    uint32_t last_log_ms = 0;
    uint32_t last_sweep_ms = 0;
    uint8_t sweep_face = 0;

    while (1) {
        bool running;
        ir_raw_mode_t mode;
        ir_raw_face_t fixed_face;

        if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            running = s_ctx.running;
            mode = s_ctx.mode;
            fixed_face = s_ctx.face;
            xSemaphoreGive(s_mutex);
        } else {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        if (!running) {
            tx_off();
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);

        if (mode == IR_RAW_MODE_TX_FIXED) {
            select_face(fixed_face);
            tx_on();
            vTaskDelay(pdMS_TO_TICKS(IR_TX_ON_MS));
            tx_off();
            vTaskDelay(pdMS_TO_TICKS(IR_TX_OFF_MS));

        } else if (mode == IR_RAW_MODE_TX_SWEEP) {
            if (now_ms - last_sweep_ms >= IR_SWEEP_STEP_MS) {
                last_sweep_ms = now_ms;
                sweep_face = (sweep_face + 1) & 0x03;
            }

            select_face((ir_raw_face_t)sweep_face);
            tx_on();
            vTaskDelay(pdMS_TO_TICKS(IR_TX_ON_MS));
            tx_off();
            vTaskDelay(pdMS_TO_TICKS(IR_TX_OFF_MS));

        } else if (mode == IR_RAW_MODE_RX_FIXED) {
            select_face(fixed_face);
            add_sample(fixed_face, rx_active());
            vTaskDelay(pdMS_TO_TICKS(IR_RX_TICK_MS));

        } else if (mode == IR_RAW_MODE_RX_SWEEP) {
            if (now_ms - last_sweep_ms >= IR_SWEEP_STEP_MS) {
                last_sweep_ms = now_ms;
                sweep_face = (sweep_face + 1) & 0x03;
            }

            ir_raw_face_t face = (ir_raw_face_t)sweep_face;
            select_face(face);
            add_sample(face, rx_active());
            vTaskDelay(pdMS_TO_TICKS(IR_RX_TICK_MS));
        }

        now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        if (now_ms - last_log_ms >= IR_LOG_PERIOD_MS) {
            last_log_ms = now_ms;
            if (mode == IR_RAW_MODE_RX_FIXED || mode == IR_RAW_MODE_RX_SWEEP) {
                log_stats();
            } else {
                ESP_LOGI(TAG, "TX alive mode=%s face=%d", ir_raw_mode_name(mode), 
                         mode == IR_RAW_MODE_TX_FIXED ? fixed_face : sweep_face);
            }
        }
    }
}

static void hw_init(void)
{
    gpio_config_t out_conf = {
        .pin_bit_mask =
            (1ULL << IR_SEL0_GPIO) |
            (1ULL << IR_SEL1_GPIO) |
            (1ULL << IR_TX_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&out_conf));

    gpio_config_t in_conf = {
        .pin_bit_mask = (1ULL << IR_RX_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&in_conf));

    tx_off();
    select_face(IR_RAW_FACE_0);

    ESP_LOGI(TAG, "init SEL0=%d SEL1=%d RX=%d TX=%d RX_ACTIVE=%d",
             IR_SEL0_GPIO, IR_SEL1_GPIO, IR_RX_GPIO, IR_TX_GPIO, IR_RX_ACTIVE_LEVEL);
}

static void select_face(ir_raw_face_t face)
{
    uint8_t f = (uint8_t)face & 0x03;

    gpio_set_level(IR_SEL0_GPIO, f & 0x01);
    gpio_set_level(IR_SEL1_GPIO, (f >> 1) & 0x01);
}

static void tx_on(void)
{
    gpio_set_level(IR_TX_GPIO, 1);
}

static void tx_off(void)
{
    gpio_set_level(IR_TX_GPIO, 0);
}

static bool rx_active(void)
{
    return gpio_get_level(IR_RX_GPIO) == IR_RX_ACTIVE_LEVEL;
}

static void reset_stats(void)
{
    memset(&s_ctx.stats, 0, sizeof(s_ctx.stats));
    s_ctx.stats.best_face = IR_RAW_FACE_0;
    s_ctx.stats.best_hits = 0;
    s_ctx.stats.any_hit = false;
}

static void add_sample(ir_raw_face_t face, bool hit)
{
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return;
    }

    uint8_t f = (uint8_t)face & 0x03;

    s_ctx.stats.samples[f]++;

    if (hit) {
        s_ctx.stats.hits[f]++;
        s_ctx.stats.any_hit = true;
    }

    uint32_t best_hits = 0;
    uint8_t best_face = 0;

    for (uint8_t i = 0; i < 4; i++) {
        if (s_ctx.stats.hits[i] > best_hits) {
            best_hits = s_ctx.stats.hits[i];
            best_face = i;
        }
    }

    s_ctx.stats.best_face = (ir_raw_face_t)best_face;
    s_ctx.stats.best_hits = best_hits;

    xSemaphoreGive(s_mutex);
}

static void log_stats(void)
{
    ir_raw_stats_t st;
    if (!ir_raw_test_get_stats(&st)) {
        return;
    }

    ESP_LOGI(TAG,
             "RX stats hits=[%lu,%lu,%lu,%lu] samples=[%lu,%lu,%lu,%lu] best_face=%d best_hits=%lu any=%d",
             (unsigned long)st.hits[0],
             (unsigned long)st.hits[1],
             (unsigned long)st.hits[2],
             (unsigned long)st.hits[3],
             (unsigned long)st.samples[0],
             (unsigned long)st.samples[1],
             (unsigned long)st.samples[2],
             (unsigned long)st.samples[3],
             st.best_face,
             (unsigned long)st.best_hits,
             st.any_hit ? 1 : 0);
}

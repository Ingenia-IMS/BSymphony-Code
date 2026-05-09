#include "leds/led_manager.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_random.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// -----------------------------------------------------------------------------
// CONFIG NUEVO CUBO
// -----------------------------------------------------------------------------

#define LED_GPIO                  GPIO_NUM_16   // D6
#define LED_PHYSICAL_COUNT        4
#define LED_ACTIVE_COUNT          4

#define LED_TASK_STACK_WORDS      3072
#define LED_TASK_PRIORITY         1
#define LED_TASK_PERIOD_MS        100

#define LED_RMT_RESOLUTION_HZ     10000000     // 10 MHz

#define BLINK_HALF_PERIOD_MS      125

// Si tras esto sigue dando problemas, prueba a poner 0 para desactivar logs
// repetitivos de RMT.
#define LED_LOG_RMT_ERRORS        1

// Orden físico nuevo:
// 0 = arriba izquierda
// 1 = abajo izquierda
// 2 = abajo derecha
// 3 = arriba derecha
static const uint8_t LOGICAL_TO_PHYSICAL[LED_ACTIVE_COUNT] = {0, 1, 2, 3};

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} rgb_t;

typedef enum {
    EFFECT_OFF = 0,
    EFFECT_SOLID,
    EFFECT_DIAGONAL_DUAL,
    EFFECT_STORM,
    EFFECT_FIRE,
    EFFECT_WATER,
    EFFECT_RAINBOW,
    EFFECT_ELECTRICITY,
} led_effect_t;

typedef struct {
    bool initialized;

    rmt_channel_handle_t rmt_chan;
    rmt_encoder_handle_t rmt_encoder;

    TaskHandle_t task_handle;
    portMUX_TYPE lock;

    uint8_t master_brightness;
    bool blink_enabled;

    led_effect_t effect;
    rgb_t primary;
    rgb_t secondary;

    uint32_t effect_start_ms;

    uint32_t next_event_ms;
    uint32_t event_end_ms;
    uint32_t extra_event_ms;
    uint8_t event_mask;

    rgb_t cached_frame[LED_ACTIVE_COUNT];

    uint8_t tx_buffer[LED_PHYSICAL_COUNT * 3]; // GRB

    bool rmt_busy;
    uint32_t last_rmt_error_ms;
} led_state_t;

static const char *TAG = "LED";

static led_state_t s_led = {
    .initialized = false,
    .rmt_chan = NULL,
    .rmt_encoder = NULL,
    .task_handle = NULL,
    .lock = portMUX_INITIALIZER_UNLOCKED,
    .master_brightness = 100,
    .blink_enabled = false,
    .effect = EFFECT_OFF,
    .rmt_busy = false,
    .last_rmt_error_ms = 0,
};

// -----------------------------------------------------------------------------
// HELPERS
// -----------------------------------------------------------------------------

static uint32_t now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static rgb_t rgb_make(uint8_t r, uint8_t g, uint8_t b)
{
    rgb_t c = {.r = r, .g = g, .b = b};
    return c;
}

static rgb_t color_from_enum(led_color_t color)
{
    switch (color) {
        case LED_COLOR_RED:          return rgb_make(255,   0,   0);
        case LED_COLOR_GREEN:        return rgb_make(  0, 255,   0);
        case LED_COLOR_BLUE:         return rgb_make(  0,   0, 255);
        case LED_COLOR_LIGHT_BLUE:   return rgb_make( 80, 180, 255);
        case LED_COLOR_CYAN:         return rgb_make(  0, 255, 255);
        case LED_COLOR_WHITE:        return rgb_make(255, 255, 255);
        case LED_COLOR_WARM_WHITE:   return rgb_make(255, 180, 100);
        case LED_COLOR_YELLOW:       return rgb_make(255, 220,   0);
        case LED_COLOR_ORANGE:       return rgb_make(255, 120,   0);
        case LED_COLOR_PINK:         return rgb_make(255,  40, 120);
        case LED_COLOR_PURPLE:       return rgb_make(140,   0, 180);
        case LED_COLOR_DARK_BLUE:    return rgb_make(  0,  20,  70);
        case LED_COLOR_BROWN:        return rgb_make(139,  69,  19);
        case LED_COLOR_DARK_BROWN:   return rgb_make(101,  67,  33);
        case LED_COLOR_LIGHT_BROWN:  return rgb_make(181, 101,  29);
        case LED_COLOR_OFF:
        default:                     return rgb_make(  0,   0,   0);
    }
}

static uint8_t scale_u8(uint8_t value, uint8_t brightness)
{
    return (uint8_t)(((uint16_t)value * (uint16_t)brightness) / 255U);
}

static rgb_t scale_rgb(rgb_t c, uint8_t brightness)
{
    return rgb_make(
        scale_u8(c.r, brightness),
        scale_u8(c.g, brightness),
        scale_u8(c.b, brightness)
    );
}

static uint8_t lerp_u8(uint8_t a, uint8_t b, uint8_t t)
{
    int16_t diff = (int16_t)b - (int16_t)a;
    return (uint8_t)(a + ((diff * t) / 255));
}

static rgb_t lerp_rgb(rgb_t a, rgb_t b, uint8_t t)
{
    return rgb_make(
        lerp_u8(a.r, b.r, t),
        lerp_u8(a.g, b.g, t),
        lerp_u8(a.b, b.b, t)
    );
}

static uint8_t triwave8(uint32_t period_ms, uint32_t now, uint32_t phase_ms)
{
    if (period_ms < 2U) return 0;

    uint32_t x = (now + phase_ms) % period_ms;
    uint32_t half = period_ms / 2U;

    if (x < half) {
        return (uint8_t)((x * 255U) / half);
    }

    return (uint8_t)(((period_ms - x) * 255U) / half);
}

static rgb_t wheel(uint8_t pos)
{
    if (pos < 85) {
        return rgb_make((uint8_t)(255 - pos * 3), (uint8_t)(pos * 3), 0);
    }

    if (pos < 170) {
        pos = (uint8_t)(pos - 85);
        return rgb_make(0, (uint8_t)(255 - pos * 3), (uint8_t)(pos * 3));
    }

    pos = (uint8_t)(pos - 170);
    return rgb_make((uint8_t)(pos * 3), 0, (uint8_t)(255 - pos * 3));
}

static bool blink_visible(uint32_t now)
{
    if (!s_led.blink_enabled) return true;
    return (((now / BLINK_HALF_PERIOD_MS) & 1U) == 0U);
}

static void clear_frame(rgb_t frame[LED_ACTIVE_COUNT])
{
    for (int i = 0; i < LED_ACTIVE_COUNT; i++) {
        frame[i] = rgb_make(0, 0, 0);
    }
}

// -----------------------------------------------------------------------------
// RMT CALLBACK
// -----------------------------------------------------------------------------

static bool IRAM_ATTR rmt_tx_done_callback(
    rmt_channel_handle_t channel,
    const rmt_tx_done_event_data_t *edata,
    void *user_ctx
)
{
    (void)channel;
    (void)edata;

    led_state_t *state = (led_state_t *)user_ctx;
    state->rmt_busy = false;

    return false;
}

// -----------------------------------------------------------------------------
// RMT WRITE
// -----------------------------------------------------------------------------

static void write_frame_to_strip(const rgb_t frame[LED_ACTIVE_COUNT], bool visible)
{
    if (!s_led.initialized || s_led.rmt_chan == NULL || s_led.rmt_encoder == NULL) {
        return;
    }

    // Si la transmisión anterior aún no ha terminado, no encolamos otra.
    // Esto evita llenar la cola RMT y evita el flush timeout.
    if (s_led.rmt_busy) {
        return;
    }

    for (int logical = 0; logical < LED_ACTIVE_COUNT; logical++) {
        uint8_t physical = LOGICAL_TO_PHYSICAL[logical];

        rgb_t c = visible
            ? scale_rgb(frame[logical], s_led.master_brightness)
            : rgb_make(0, 0, 0);

        // FZ2812 / WS2812 suele usar GRB
        s_led.tx_buffer[physical * 3 + 0] = c.g;
        s_led.tx_buffer[physical * 3 + 1] = c.r;
        s_led.tx_buffer[physical * 3 + 2] = c.b;
    }

    rmt_transmit_config_t tx_config = {
        .loop_count = 0,
    };

    s_led.rmt_busy = true;

    esp_err_t err = rmt_transmit(
        s_led.rmt_chan,
        s_led.rmt_encoder,
        s_led.tx_buffer,
        sizeof(s_led.tx_buffer),
        &tx_config
    );

    if (err != ESP_OK) {
        s_led.rmt_busy = false;

#if LED_LOG_RMT_ERRORS
        uint32_t now = now_ms();

        // Evita spamear el puerto serie si algo va mal.
        if (now - s_led.last_rmt_error_ms > 1000U) {
            s_led.last_rmt_error_ms = now;
            ESP_LOGE(TAG, "rmt_transmit failed: %s", esp_err_to_name(err));
        }
#endif
        return;
    }

    // No usamos rmt_tx_wait_all_done().
    // La liberación de rmt_busy se hace en el callback on_trans_done.

    // Reset/latch >300 us. Como la tarea espera 100 ms, sobra.
    vTaskDelay(pdMS_TO_TICKS(1));
}

// -----------------------------------------------------------------------------
// EFECTOS
// -----------------------------------------------------------------------------

static void effect_off(rgb_t frame[LED_ACTIVE_COUNT])
{
    clear_frame(frame);
}

static void effect_solid(rgb_t frame[LED_ACTIVE_COUNT])
{
    for (int i = 0; i < LED_ACTIVE_COUNT; i++) {
        frame[i] = s_led.primary;
    }
}

static void effect_diagonal_dual(rgb_t frame[LED_ACTIVE_COUNT], uint32_t now)
{
    uint8_t t = triwave8(700, now, s_led.effect_start_ms);

    rgb_t diag_a = lerp_rgb(s_led.primary, s_led.secondary, t);
    rgb_t diag_b = lerp_rgb(s_led.secondary, s_led.primary, t);

    // Orden nuevo:
    // 0 3
    // 1 2
    //
    // Diagonales:
    // A = 0 y 2
    // B = 1 y 3
    frame[0] = diag_a;
    frame[2] = diag_a;

    frame[1] = diag_b;
    frame[3] = diag_b;
}

static void effect_storm(rgb_t frame[LED_ACTIVE_COUNT], uint32_t now)
{
    rgb_t dark_a = rgb_make(0, 12, 45);
    rgb_t dark_b = rgb_make(0, 35, 95);
    rgb_t dark_c = rgb_make(10, 60, 120);

    frame[0] = lerp_rgb(dark_a, dark_b, triwave8(1800, now, 0));
    frame[1] = lerp_rgb(dark_b, dark_c, triwave8(2300, now, 450));
    frame[2] = lerp_rgb(dark_a, dark_c, triwave8(1500, now, 900));
    frame[3] = lerp_rgb(dark_b, dark_a, triwave8(1600, now, 300));

    if (now >= s_led.next_event_ms && now > s_led.event_end_ms) {
        s_led.event_end_ms = now + 50U + (esp_random() % 50U);
        s_led.extra_event_ms = s_led.event_end_ms + 70U + (esp_random() % 80U);
        s_led.next_event_ms = now + 1800U + (esp_random() % 2500U);
    }

    if ((now < s_led.event_end_ms) ||
        (now >= s_led.extra_event_ms && now < (s_led.extra_event_ms + 40U))) {
        for (int i = 0; i < LED_ACTIVE_COUNT; i++) {
            frame[i] = rgb_make(255, 255, 240);
        }
    }
}

static void effect_fire(rgb_t frame[LED_ACTIVE_COUNT], uint32_t now)
{
    if (now >= s_led.next_event_ms) {
        s_led.next_event_ms = now + 45U + (esp_random() % 45U);

        for (int i = 0; i < LED_ACTIVE_COUNT; i++) {
            uint32_t r = esp_random() % 100U;

            if (r < 20U) {
                s_led.cached_frame[i] = rgb_make(255, 210, 40);
            } else if (r < 65U) {
                s_led.cached_frame[i] = rgb_make(255, 110, 0);
            } else {
                s_led.cached_frame[i] = rgb_make(180, 15, 0);
            }
        }
    }

    for (int i = 0; i < LED_ACTIVE_COUNT; i++) {
        frame[i] = s_led.cached_frame[i];
    }
}

static void effect_water(rgb_t frame[LED_ACTIVE_COUNT], uint32_t now)
{
    rgb_t a = rgb_make(0, 40, 120);
    rgb_t b = rgb_make(0, 110, 255);
    rgb_t c = rgb_make(0, 180, 255);

    frame[0] = lerp_rgb(a, b, triwave8(1400, now,   0));
    frame[1] = lerp_rgb(b, c, triwave8(1700, now, 200));
    frame[2] = lerp_rgb(a, c, triwave8(1500, now, 500));
    frame[3] = lerp_rgb(b, a, triwave8(1600, now, 800));
}

static void effect_rainbow(rgb_t frame[LED_ACTIVE_COUNT], uint32_t now)
{
    uint8_t base = (uint8_t)((now / 12U) & 0xFFU);

    frame[0] = wheel((uint8_t)(base +   0));
    frame[1] = wheel((uint8_t)(base +  64));
    frame[2] = wheel((uint8_t)(base + 128));
    frame[3] = wheel((uint8_t)(base + 192));
}

static void effect_electricity(rgb_t frame[LED_ACTIVE_COUNT], uint32_t now)
{
    rgb_t base = rgb_make(110, 110, 125);

    for (int i = 0; i < LED_ACTIVE_COUNT; i++) {
        frame[i] = base;
    }

    if (now >= s_led.next_event_ms && now > s_led.event_end_ms) {
        s_led.event_end_ms = now + 40U + (esp_random() % 50U);
        s_led.next_event_ms = now + 400U + (esp_random() % 900U);

        uint32_t pick = esp_random() % 6U;

        switch (pick) {
            case 0: s_led.event_mask = 0x1; break;
            case 1: s_led.event_mask = 0x2; break;
            case 2: s_led.event_mask = 0x4; break;
            case 3: s_led.event_mask = 0x8; break;
            case 4: s_led.event_mask = 0x5; break;
            default: s_led.event_mask = 0xA; break;
        }
    }

    if (now < s_led.event_end_ms) {
        for (int i = 0; i < LED_ACTIVE_COUNT; i++) {
            if (s_led.event_mask & (1U << i)) {
                frame[i] = rgb_make(255, 220, 40);
            }
        }
    }
}

static void build_base_frame(rgb_t frame[LED_ACTIVE_COUNT], uint32_t now)
{
    switch (s_led.effect) {
        case EFFECT_OFF:
            effect_off(frame);
            break;

        case EFFECT_SOLID:
            effect_solid(frame);
            break;

        case EFFECT_DIAGONAL_DUAL:
            effect_diagonal_dual(frame, now);
            break;

        case EFFECT_STORM:
            effect_storm(frame, now);
            break;

        case EFFECT_FIRE:
            effect_fire(frame, now);
            break;

        case EFFECT_WATER:
            effect_water(frame, now);
            break;

        case EFFECT_RAINBOW:
            effect_rainbow(frame, now);
            break;

        case EFFECT_ELECTRICITY:
            effect_electricity(frame, now);
            break;

        default:
            effect_off(frame);
            break;
    }
}

// -----------------------------------------------------------------------------
// TASK
// -----------------------------------------------------------------------------

static void led_task(void *arg)
{
    (void)arg;

    rgb_t frame[LED_ACTIVE_COUNT];

    while (1) {
        uint32_t now = now_ms();

        taskENTER_CRITICAL(&s_led.lock);
        build_base_frame(frame, now);
        bool visible = blink_visible(now);
        taskEXIT_CRITICAL(&s_led.lock);

        write_frame_to_strip(frame, visible);

        vTaskDelay(pdMS_TO_TICKS(LED_TASK_PERIOD_MS));
    }
}

// -----------------------------------------------------------------------------
// API
// -----------------------------------------------------------------------------

esp_err_t led_manager_init(void)
{
    if (s_led.initialized) {
        return ESP_OK;
    }

    rmt_tx_channel_config_t tx_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = LED_GPIO,
        .mem_block_symbols = 64,
        .resolution_hz = LED_RMT_RESOLUTION_HZ,

        // Importante: no queremos cola larga.
        // Si algo va mal, una cola de 4 acaba acumulando transmisiones.
        .trans_queue_depth = 1,
    };

    esp_err_t err = rmt_new_tx_channel(&tx_config, &s_led.rmt_chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_new_tx_channel failed: %s", esp_err_to_name(err));
        return err;
    }

    rmt_bytes_encoder_config_t encoder_config = {
        .bit0 = {
            .level0 = 1,
            .duration0 = 4,
            .level1 = 0,
            .duration1 = 8,
        },
        .bit1 = {
            .level0 = 1,
            .duration0 = 8,
            .level1 = 0,
            .duration1 = 4,
        },
        .flags.msb_first = 1,
    };

    err = rmt_new_bytes_encoder(&encoder_config, &s_led.rmt_encoder);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_new_bytes_encoder failed: %s", esp_err_to_name(err));
        return err;
    }

    rmt_tx_event_callbacks_t callbacks = {
        .on_trans_done = rmt_tx_done_callback,
    };

    err = rmt_tx_register_event_callbacks(
        s_led.rmt_chan,
        &callbacks,
        &s_led
    );

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_tx_register_event_callbacks failed: %s", esp_err_to_name(err));
        return err;
    }

    err = rmt_enable(s_led.rmt_chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_enable failed: %s", esp_err_to_name(err));
        return err;
    }

    s_led.initialized = true;
    s_led.effect = EFFECT_OFF;
    s_led.master_brightness = 100;
    s_led.blink_enabled = false;
    s_led.primary = rgb_make(0, 0, 0);
    s_led.secondary = rgb_make(0, 0, 0);
    s_led.effect_start_ms = now_ms();
    s_led.next_event_ms = s_led.effect_start_ms + 1000U;
    s_led.event_end_ms = 0;
    s_led.extra_event_ms = 0;
    s_led.event_mask = 0;
    s_led.rmt_busy = false;
    s_led.last_rmt_error_ms = 0;

    memset(s_led.cached_frame, 0, sizeof(s_led.cached_frame));
    memset(s_led.tx_buffer, 0, sizeof(s_led.tx_buffer));

    BaseType_t ok = xTaskCreate(
        led_task,
        "led_task",
        LED_TASK_STACK_WORDS,
        NULL,
        LED_TASK_PRIORITY,
        &s_led.task_handle
    );

    if (ok != pdPASS) {
        ESP_LOGE(TAG, "No se pudo crear led_task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "LED manager inicializado en GPIO %d", LED_GPIO);
    return ESP_OK;
}

esp_err_t led_manager_set_master_brightness(uint8_t brightness)
{
    taskENTER_CRITICAL(&s_led.lock);
    s_led.master_brightness = brightness;
    taskEXIT_CRITICAL(&s_led.lock);

    return ESP_OK;
}

esp_err_t led_manager_set_blink_enabled(bool enabled)
{
    taskENTER_CRITICAL(&s_led.lock);
    s_led.blink_enabled = enabled;
    taskEXIT_CRITICAL(&s_led.lock);

    return ESP_OK;
}

bool led_manager_is_blink_enabled(void)
{
    bool enabled;

    taskENTER_CRITICAL(&s_led.lock);
    enabled = s_led.blink_enabled;
    taskEXIT_CRITICAL(&s_led.lock);

    return enabled;
}

esp_err_t led_manager_set_off(void)
{
    taskENTER_CRITICAL(&s_led.lock);
    s_led.effect = EFFECT_OFF;
    s_led.effect_start_ms = now_ms();
    taskEXIT_CRITICAL(&s_led.lock);

    return ESP_OK;
}

esp_err_t led_manager_set_solid(led_color_t color)
{
    taskENTER_CRITICAL(&s_led.lock);
    s_led.effect = EFFECT_SOLID;
    s_led.primary = color_from_enum(color);
    s_led.effect_start_ms = now_ms();
    taskEXIT_CRITICAL(&s_led.lock);

    return ESP_OK;
}

esp_err_t led_manager_set_diagonal_dual(led_color_t color_a, led_color_t color_b)
{
    taskENTER_CRITICAL(&s_led.lock);
    s_led.effect = EFFECT_DIAGONAL_DUAL;
    s_led.primary = color_from_enum(color_a);
    s_led.secondary = color_from_enum(color_b);
    s_led.effect_start_ms = now_ms();
    taskEXIT_CRITICAL(&s_led.lock);

    return ESP_OK;
}

esp_err_t led_manager_set_storm(void)
{
    taskENTER_CRITICAL(&s_led.lock);
    s_led.effect = EFFECT_STORM;
    s_led.effect_start_ms = now_ms();
    s_led.next_event_ms = s_led.effect_start_ms + 1200U;
    s_led.event_end_ms = 0;
    s_led.extra_event_ms = 0;
    taskEXIT_CRITICAL(&s_led.lock);

    return ESP_OK;
}

esp_err_t led_manager_set_fire(void)
{
    taskENTER_CRITICAL(&s_led.lock);
    s_led.effect = EFFECT_FIRE;
    s_led.effect_start_ms = now_ms();
    s_led.next_event_ms = 0;

    for (int i = 0; i < LED_ACTIVE_COUNT; i++) {
        s_led.cached_frame[i] = rgb_make(255, 110, 0);
    }

    taskEXIT_CRITICAL(&s_led.lock);

    return ESP_OK;
}

esp_err_t led_manager_set_water(void)
{
    taskENTER_CRITICAL(&s_led.lock);
    s_led.effect = EFFECT_WATER;
    s_led.effect_start_ms = now_ms();
    taskEXIT_CRITICAL(&s_led.lock);

    return ESP_OK;
}

esp_err_t led_manager_set_rainbow(void)
{
    taskENTER_CRITICAL(&s_led.lock);
    s_led.effect = EFFECT_RAINBOW;
    s_led.effect_start_ms = now_ms();
    taskEXIT_CRITICAL(&s_led.lock);

    return ESP_OK;
}

esp_err_t led_manager_set_electricity(void)
{
    taskENTER_CRITICAL(&s_led.lock);
    s_led.effect = EFFECT_ELECTRICITY;
    s_led.effect_start_ms = now_ms();
    s_led.next_event_ms = s_led.effect_start_ms + 300U;
    s_led.event_end_ms = 0;
    s_led.event_mask = 0;
    taskEXIT_CRITICAL(&s_led.lock);

    return ESP_OK;
}
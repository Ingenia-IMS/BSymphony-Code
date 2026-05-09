#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_timer.h"

#include "imu/imu_manager.h"
#include "leds/led_manager.h"
#include "sonido/sound_player.h"
#include "elementos/element_catalog.h"
#include "elementos/cube_state.h"
#include "IR/ir_link.h"

#define MAIN_TASK_DELAY_MS 10
#define LED_BRIGHTNESS_20_PERCENT 51

static const char *TAG = "MAIN";

static const char *element_sequence[] = {
    "agua",
    "electricidad",
    "fuego",
    "humano",
    "metal",
    "mono",
    "naturaleza",
    "oeste",
    "pajaro",
    "piedra",
    "pistola",
    "reggaeton",
    "robot",
    "rock",
    "tormenta",
    "viento",
};

static const size_t element_sequence_count =
    sizeof(element_sequence) / sizeof(element_sequence[0]);

static size_t current_sequence_index = 0;
static volatile bool ir_search_requested = false;
static volatile bool pickup_requested = false;

static bool ir_visual_ready = false;
static ir_role_t ir_visual_role = IR_ROLE_UNKNOWN;
static uint64_t ir_visual_sync_time_us = 0;
static bool ir_visual_last_on = false;

static void change_to_sequence_element(size_t index)
{
    if (element_sequence_count == 0) {
        ESP_LOGE(TAG, "La secuencia de elementos está vacía");
        return;
    }

    if (index >= element_sequence_count) {
        index = 0;
    }

    const char *element_name = element_sequence[index];

    ESP_LOGI(TAG, "Cambiando a elemento de la secuencia: %s", element_name);

    if (cube_state_set_element_by_name(element_name)) {
        cube_state_play_current_sound();
        current_sequence_index = index;
    } else {
        ESP_LOGW(TAG, "El elemento '%s' no existe en el catálogo", element_name);
    }
}

static void change_to_next_sequence_element(void)
{
    size_t next_index = current_sequence_index + 1;

    if (next_index >= element_sequence_count) {
        next_index = 0;
    }

    change_to_sequence_element(next_index);
}

static void restore_current_element_visual(void)
{
    ir_visual_ready = false;
    led_manager_set_blink_enabled(false);

    const char *name = cube_state_get_current_name();
    if (name != NULL) {
        cube_state_set_element_by_name(name);
    } else {
        led_manager_set_off();
    }
}

static void set_locked_face_color(ir_face_t face)
{
    led_manager_set_blink_enabled(false);

    switch (face) {
        case IR_FACE_0:
            led_manager_set_solid(LED_COLOR_BLUE);
            break;
        case IR_FACE_1:
            led_manager_set_solid(LED_COLOR_LIGHT_BLUE);
            break;
        case IR_FACE_2:
            led_manager_set_solid(LED_COLOR_PURPLE);
            break;
        case IR_FACE_3:
            led_manager_set_solid(LED_COLOR_PINK);
            break;
        default:
            led_manager_set_solid(LED_COLOR_WHITE);
            break;
    }
}

static void update_synced_visual(void)
{
    if (!ir_visual_ready) {
        return;
    }

    uint64_t now = (uint64_t)esp_timer_get_time();
    uint64_t dt = now - ir_visual_sync_time_us;

    bool on = (dt % 400000ULL) < 120000ULL;
    if (on == ir_visual_last_on) {
        return;
    }

    ir_visual_last_on = on;

    if (!on) {
        led_manager_set_off();
        return;
    }

    if (ir_visual_role == IR_ROLE_LEADER) {
        led_manager_set_solid(LED_COLOR_GREEN);
    } else if (ir_visual_role == IR_ROLE_FOLLOWER) {
        led_manager_set_solid(LED_COLOR_CYAN);
    } else {
        led_manager_set_solid(LED_COLOR_WHITE);
    }
}

static void handle_ir_event(const ir_event_t *ev)
{
    ESP_LOGI(
        TAG,
        "IR event=%s state=%s face=%d role=%s",
        ir_link_event_name(ev->type),
        ir_link_state_name(ev->state),
        ev->face,
        ir_link_role_name(ev->role)
    );

    switch (ev->type) {
        case IR_EVENT_SEARCH_STARTED:
            ir_visual_ready = false;
            led_manager_set_blink_enabled(true);
            led_manager_set_solid(LED_COLOR_YELLOW);
            break;

        case IR_EVENT_CANDIDATE_FACE:
            ir_visual_ready = false;
            led_manager_set_blink_enabled(true);
            led_manager_set_solid(LED_COLOR_ORANGE);
            break;

        case IR_EVENT_FACE_LOCKED:
            ir_visual_ready = false;
            set_locked_face_color(ev->face);
            break;

        case IR_EVENT_SYNCED:
            ir_visual_ready = true;
            ir_visual_role = ev->role;
            ir_visual_sync_time_us = ev->sync_time_us;
            ir_visual_last_on = false;
            break;

        case IR_EVENT_SEARCH_TIMEOUT:
            ir_visual_ready = false;
            led_manager_set_blink_enabled(false);
            led_manager_set_solid(LED_COLOR_RED);
            vTaskDelay(pdMS_TO_TICKS(300));
            restore_current_element_visual();
            break;

        case IR_EVENT_LINK_LOST:
            ir_visual_ready = false;
            led_manager_set_blink_enabled(false);
            led_manager_set_solid(LED_COLOR_RED);
            vTaskDelay(pdMS_TO_TICKS(300));
            restore_current_element_visual();
            break;

        case IR_EVENT_STOPPED:
            restore_current_element_visual();
            break;

        default:
            break;
    }
}

static void on_pickup_detected(void)
{
    pickup_requested = true;
}

static void on_strong_shake_detected(void)
{
    ir_search_requested = true;
}

void app_main(void)
{
    ESP_LOGI(TAG, "Inicializando sistema con prueba IR fase 1...");

    imu_init();
    led_manager_init();
    led_manager_set_master_brightness(LED_BRIGHTNESS_20_PERCENT);
    sound_player_init();
    cube_state_init();
    ir_link_init();

    imu_set_pickup_callback(on_pickup_detected);
    imu_set_shake_callback(on_strong_shake_detected);

    imu_start_task();

    change_to_sequence_element(0);

    ESP_LOGI(TAG, "Elemento actual: %s", cube_state_get_current_name());
    ESP_LOGI(TAG, "Sacude dos cubos y júntalos por una cara para probar IR.");

    while (1) {
        if (pickup_requested) {
            pickup_requested = false;

            ESP_LOGI(
                TAG,
                "Mover suave -> sonido del elemento actual: %s",
                cube_state_get_current_name()
            );

            cube_state_play_current_sound();
        }

        if (ir_search_requested) {
            ir_search_requested = false;

            ESP_LOGI(TAG, "Agitado vigoroso -> empieza búsqueda IR");
            ir_link_start_search();
        }

        ir_event_t ev;
        while (ir_link_get_event(&ev, 0)) {
            handle_ir_event(&ev);
        }

        update_synced_visual();

        vTaskDelay(pdMS_TO_TICKS(MAIN_TASK_DELAY_MS));
    }
}

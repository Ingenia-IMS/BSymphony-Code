#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_timer.h"

#include "leds/led_manager.h"
#include "imu/imu_manager.h"
#include "sonido/sound_player.h"
#include "elementos/cube_state.h"
#include "IR/ir_link.h"

#define LED_BRIGHTNESS_20_PERCENT 51
#define MAIN_TASK_DELAY_MS        10

static const char *TAG = "MAIN_IR_PHASE3";

static volatile bool s_shake_requested = false;
static volatile bool s_pickup_requested = false;

static bool s_ir_ready_visual = false;
static ir_role_t s_visual_role = IR_ROLE_UNKNOWN;
static uint64_t s_visual_sync_time_us = 0;
static bool s_visual_last_on = false;

static void on_imu_shake(void)
{
    s_shake_requested = true;
}

static void on_imu_pickup(void)
{
    s_pickup_requested = true;
}

static bool ir_is_active(void)
{
    ir_status_t st;
    if (!ir_link_get_status(&st)) {
        return false;
    }

    return st.state != IR_LINK_IDLE;
}

static void debug_led_searching(void)
{
    s_ir_ready_visual = false;
    led_manager_set_blink_enabled(true);
    led_manager_set_solid(LED_COLOR_YELLOW);
}

static void debug_led_candidate(ir_face_t face)
{
    (void)face;
    s_ir_ready_visual = false;
    led_manager_set_blink_enabled(true);
    led_manager_set_solid(LED_COLOR_ORANGE);
}

static void debug_led_locked(ir_face_t face)
{
    s_ir_ready_visual = false;
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

static void debug_led_synced_start(ir_role_t role, uint64_t sync_time_us)
{
    led_manager_set_blink_enabled(false);
    s_ir_ready_visual = true;
    s_visual_role = role;
    s_visual_sync_time_us = sync_time_us;
    s_visual_last_on = false;
}

static void debug_led_idle_from_current_element(void)
{
    s_ir_ready_visual = false;
    led_manager_set_blink_enabled(false);

    const char *name = cube_state_get_current_name();
    if (name != NULL) {
        cube_state_set_element_by_name(name);
    } else {
        led_manager_set_off();
    }
}

static void debug_led_error_red(void)
{
    s_ir_ready_visual = false;
    led_manager_set_blink_enabled(false);
    led_manager_set_solid(LED_COLOR_RED);
}

static void debug_update_synced_led(void)
{
    if (!s_ir_ready_visual) {
        return;
    }

    uint64_t now = (uint64_t)esp_timer_get_time();
    uint64_t dt = now - s_visual_sync_time_us;

    bool on = (dt % 450000ULL) < 150000ULL;

    if (on == s_visual_last_on) {
        return;
    }

    s_visual_last_on = on;

    if (!on) {
        led_manager_set_off();
        return;
    }

    if (s_visual_role == IR_ROLE_LEADER) {
        led_manager_set_solid(LED_COLOR_GREEN);
    } else if (s_visual_role == IR_ROLE_FOLLOWER) {
        led_manager_set_solid(LED_COLOR_CYAN);
    } else {
        led_manager_set_solid(LED_COLOR_WHITE);
    }
}

static void handle_ir_event(const ir_event_t *ev)
{
    ESP_LOGI(TAG,
             "IR event=%s state=%s face=%d(%s) role=%s",
             ir_link_event_name(ev->type),
             ir_link_state_name(ev->state),
             ev->face,
             ir_link_face_name(ev->face),
             ir_link_role_name(ev->role));

    switch (ev->type) {
        case IR_EVENT_SEARCH_STARTED:
            debug_led_searching();
            break;

        case IR_EVENT_CANDIDATE_FACE:
            debug_led_candidate(ev->face);
            break;

        case IR_EVENT_FACE_LOCKED:
            debug_led_locked(ev->face);
            break;

        case IR_EVENT_SYNCED:
            debug_led_synced_start(ev->role, ev->sync_time_us);
            break;

        case IR_EVENT_REMOTE_ELEMENT_RX:
            ESP_LOGI(TAG,
                     "Elemento remoto recibido por IR: id=%u name=%s",
                     ev->remote_element_id,
                     ev->remote_element_name);
            /*
             * Para depuración visual: un flash blanco muy breve.
             * No cambiamos el elemento local todavía.
             */
            led_manager_set_solid(LED_COLOR_WHITE);
            vTaskDelay(pdMS_TO_TICKS(80));
            break;

        case IR_EVENT_SEARCH_TIMEOUT:
            ESP_LOGI(TAG, "IR timeout: no se encontró otro cubo");
            debug_led_error_red();
            vTaskDelay(pdMS_TO_TICKS(300));
            debug_led_idle_from_current_element();
            break;

        case IR_EVENT_LINK_LOST:
            ESP_LOGI(TAG, "IR perdido: 3 s sin ver al otro cubo");
            debug_led_error_red();
            vTaskDelay(pdMS_TO_TICKS(300));
            debug_led_idle_from_current_element();
            break;

        case IR_EVENT_STOPPED:
            debug_led_idle_from_current_element();
            break;

        default:
            break;
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Prueba IR fase 3: envío de elemento remoto");

    ESP_ERROR_CHECK(led_manager_init());
    ESP_ERROR_CHECK(led_manager_set_master_brightness(LED_BRIGHTNESS_20_PERCENT));
    ESP_ERROR_CHECK(led_manager_set_blink_enabled(false));
    ESP_ERROR_CHECK(led_manager_set_off());

    sound_player_init();

    cube_state_init();

    imu_init();
    imu_set_pickup_callback(on_imu_pickup);
    imu_set_shake_callback(on_imu_shake);
    imu_start_task();

    ir_link_init();
    ir_link_set_local_element_name(cube_state_get_current_name());

    ESP_LOGI(TAG, "Listo. Sacude dos cubos y júntalos por una cara.");

    while (1) {
        /*
         * Mantener actualizado el nombre del elemento que se anuncia por IR.
         * Ahora mismo normalmente será "agua", salvo que tú cambies el estado en otro sitio.
         */
        ir_link_set_local_element_name(cube_state_get_current_name());

        if (s_pickup_requested) {
            s_pickup_requested = false;

            if (!ir_is_active()) {
                ESP_LOGI(TAG, "Mover suave -> sonido del elemento actual: %s", cube_state_get_current_name());
                cube_state_play_current_sound();
            } else {
                ESP_LOGI(TAG, "Mover suave ignorado: IR activo");
            }
        }

        if (s_shake_requested) {
            s_shake_requested = false;

            ir_status_t st;
            if (ir_link_get_status(&st) && st.state == IR_LINK_IDLE) {
                ESP_LOGI(TAG, "Agitado vigoroso -> empieza búsqueda IR");
                ir_link_start_search();
                debug_led_searching();
            } else {
                ESP_LOGI(TAG, "Agitado ignorado: IR no está idle");
            }
        }

        ir_event_t ev;
        while (ir_link_get_event(&ev, 0)) {
            handle_ir_event(&ev);
        }

        debug_update_synced_led();

        vTaskDelay(pdMS_TO_TICKS(MAIN_TASK_DELAY_MS));
    }
}

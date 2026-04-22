#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_pdm.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "../sounds/FireForest.h"
#include "io.h"
#include "testing.h"

#define SPK_OUT_PIN    D3
#define SPK_ENA_PIN    D9   
#define PDM_DOUT_PIN   SPK_OUT_PIN
#define PDM_CLK_PIN    D6   
#define SAMPLE_RATE    48000
#define DMA_BUF_COUNT  8
#define DMA_BUF_LEN    512



static const char *TAG = "pdm_audio";

void app_main(void) {
    // 0. Configurar pines GPIO para el altavoz y habilitar amplifricador
    gpio_set_direction(SPK_OUT_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(SPK_ENA_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(SPK_ENA_PIN, 1);  // 1 = alto, 0 = bajo

    // 1. Crear canal TX
    i2s_chan_handle_t tx_handle;

    i2s_chan_config_t chan_cfg = {
        .id            = I2S_NUM_0,
        .role          = I2S_ROLE_MASTER,
        .dma_desc_num  = DMA_BUF_COUNT,
        .dma_frame_num = DMA_BUF_LEN,
        .auto_clear    = true,
    };
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle, NULL));

    // 2. Configurar modo PDM TX
    i2s_pdm_tx_config_t pdm_cfg = {
        // .clk_cfg  = I2S_PDM_TX_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .clk_cfg = {
            .sample_rate_hz = SAMPLE_RATE,
            .clk_src        = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple  = I2S_MCLK_MULTIPLE_512,
            .up_sample_fp   = 960,
            .up_sample_fs   = 480,
        },

        .slot_cfg = I2S_PDM_TX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                    I2S_SLOT_MODE_MONO),

        .gpio_cfg = {
            .clk  = PDM_CLK_PIN,
            .dout = PDM_DOUT_PIN,
            .invert_flags = {
                .clk_inv = false,
            },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_pdm_tx_mode(tx_handle, &pdm_cfg));

    // 3. Habilitar canal
    ESP_ERROR_CHECK(i2s_channel_enable(tx_handle));

    ESP_LOGI(TAG, "PDM TX iniciado a %d Hz", SAMPLE_RATE);

    // 4. Bucle de reproducción
    const size_t total_bytes = audio_data_len * sizeof(int16_t);
    const uint8_t *ptr = (const uint8_t *)audio_data;

    while (1) {
        //generate_square_wave(SPK_OUT_PIN, 440, 100); // onda cuadrada de 440 Hz durante 100 ms

               
        size_t offset = 0;
        while (offset < total_bytes) {
            size_t bytes_written = 0;
            size_t chunk = MIN(DMA_BUF_LEN * sizeof(int16_t), total_bytes - offset);
            ESP_ERROR_CHECK(i2s_channel_write(tx_handle, ptr + offset,
                                              chunk, &bytes_written,
                                              portMAX_DELAY));
            offset += bytes_written;
        }
        ESP_LOGI(TAG, "Reproducción completada");
        
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
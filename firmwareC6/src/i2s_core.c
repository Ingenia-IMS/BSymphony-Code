#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

#include "i2s_core.h"
#include "io.h"

static const char *TAG = __FILE__;

// 1. Crear canal TX
i2s_chan_handle_t i2s_tx_handle;

void i2s_setup(void) {

    // 0. Configurar pines GPIO para el altavoz y habilitar amplifricador
    gpio_set_direction(SPK_OUT_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(SPK_ENA_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(SPK_ENA_PIN, 1);  // 1 = alto, 0 = bajo

    i2s_chan_config_t chan_cfg = {
        .id            = I2S_NUM_0,
        .role          = I2S_ROLE_MASTER,
        .dma_desc_num  = DMA_BUF_COUNT,
        .dma_frame_num = DMA_BUF_LEN,
        .auto_clear    = true,
    };

    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &i2s_tx_handle, NULL));

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
    ESP_ERROR_CHECK(i2s_channel_init_pdm_tx_mode(i2s_tx_handle, &pdm_cfg));

    // 3. Habilitar canal
    ESP_ERROR_CHECK(i2s_channel_enable(i2s_tx_handle));

    ESP_LOGI(TAG, "PDM TX iniciado a %d Hz", SAMPLE_RATE);

}

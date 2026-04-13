#pragma once
#include "io.h"

#include "driver/i2s.h"
#include "driver/i2s_pdm.h"

#define SPK_OUT_PIN    D3
#define SPK_ENA_PIN    D9   
#define PDM_DOUT_PIN   SPK_OUT_PIN
#define PDM_CLK_PIN    GPIO_NUM_NC  // D6
#define SAMPLE_RATE    48000
#define DMA_BUF_COUNT  8
#define DMA_BUF_LEN    512


extern i2s_chan_handle_t i2s_tx_handle;

#define get_i2s_tx_handle() (i2s_tx_handle) 

void i2s_setup(void);


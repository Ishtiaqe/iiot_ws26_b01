#pragma once

#include <driver/i2s_std.h>
#include <cstddef>
#include <cstdint>

// INMP441 drives the left slot when L/R is grounded. Philips I²S uses a
// one-clock delay and the mic needs two 32-bit slots per sample period.
class Microphone {
public:
  esp_err_t begin(uint32_t sampleRate, gpio_num_t clock, gpio_num_t wordSelect,
                  gpio_num_t data) {
    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    channel.dma_desc_num = 8;
    channel.dma_frame_num = 256;
    esp_err_t error = i2s_new_channel(&channel, nullptr, &receiver);
    if (error != ESP_OK)
      return error;

    i2s_std_config_t format{};
    format.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sampleRate);
    format.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
        I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO);
    format.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT;
    format.slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;
    format.gpio_cfg = {I2S_GPIO_UNUSED, clock, wordSelect, I2S_GPIO_UNUSED, data, {}};
    error = i2s_channel_init_std_mode(receiver, &format);
    if (error == ESP_OK)
      error = i2s_channel_enable(receiver);
    if (error != ESP_OK) {
      i2s_del_channel(receiver);
      receiver = nullptr;
    }
    return error;
  }

  bool read(int32_t *stereoWords, size_t wordCount) {
    size_t bytesRead = 0;
    const size_t bytesRequested = wordCount * sizeof(*stereoWords);
    const esp_err_t error = i2s_channel_read(receiver, stereoWords, bytesRequested,
                                             &bytesRead, 100);
    return error == ESP_OK && bytesRead == bytesRequested;
  }

private:
  i2s_chan_handle_t receiver = nullptr;
};

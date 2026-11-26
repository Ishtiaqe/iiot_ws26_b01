#pragma once

#include <driver/i2s_std.h>
#include <cstddef>
#include <cstdint>

/**
 * Owns the ESP-IDF standard-mode I²S receive channel used by the INMP441.
 *
 * The microphone is configured as a Philips I²S stereo stream with 32-bit
 * slots. Grounding its L/R pin selects the left slot; callers therefore read
 * interleaved left/right words and use the even-indexed words as audio.
 */
class Microphone {
public:
  /** Create, configure, and enable the I²S receiver.
   * @param sampleRate Audio frames per second (the tuner uses 16 kHz).
   * @param clock      GPIO for I²S bit clock (SCK/BCLK).
   * @param wordSelect GPIO for I²S word select (WS/LRCLK).
   * @param data       GPIO receiving microphone serial data (SD).
   * @return ESP_OK on success, otherwise the ESP-IDF error from setup.
   *
   * On partial failure the newly allocated channel is deleted so the object
   * does not retain a handle that appears usable but cannot receive audio.
   */
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

  /** Read exactly wordCount interleaved 32-bit slot words.
   *
   * This is a bounded read (100 ms timeout). A short read is treated as a
   * failure because the pitch task expects complete stereo hops.
   * @param stereoWords Destination buffer with room for wordCount values.
   * @param wordCount   Number of 32-bit words, including both channels.
   * @return true only when the driver filled the entire requested buffer.
   */
  bool read(int32_t *stereoWords, size_t wordCount) {
    size_t bytesRead = 0;
    const size_t bytesRequested = wordCount * sizeof(*stereoWords);
    const esp_err_t error = i2s_channel_read(receiver, stereoWords, bytesRequested,
                                             &bytesRead, 100);
    return error == ESP_OK && bytesRead == bytesRequested;
  }

private:
  // Created by begin(); the ESP-IDF driver owns its resources until deletion.
  i2s_chan_handle_t receiver = nullptr;
};

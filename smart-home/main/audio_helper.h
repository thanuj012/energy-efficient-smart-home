/**
 * Audio Helper for ESP32-BOX-S3 Voice Recognition
 * This file contains the proper audio initialization for voice control
 */

#ifndef AUDIO_HELPER_H
#define AUDIO_HELPER_H

#include "bsp/esp-box-3.h"
#include "esp_codec_dev.h"

#include "esp_log.h"

static const char *AUDIO_TAG = "AUDIO_HELPER";

// Audio configuration
#define SAMPLE_RATE         16000
#define SAMPLE_BITS         16
#define CHANNEL_NUM         2
#define FRAME_SIZE          512

static esp_codec_dev_handle_t codec_dev = NULL;

/**
 * Initialize audio codec for voice recognition
 */
esp_err_t audio_init(void)
{
    ESP_LOGI(AUDIO_TAG, "Initializing audio codec...");
    
    // Initialize I2C for codec
    bsp_i2c_init();
    
    // Initialize codec
    codec_dev = bsp_audio_codec_speaker_init();
    if (codec_dev == NULL) {
        ESP_LOGE(AUDIO_TAG, "Failed to create codec device");
        return ESP_FAIL;
    }
    
    // Open codec
    esp_codec_dev_open(codec_dev, NULL);
    
    // Set volume (0-100)
    esp_codec_dev_set_out_vol(codec_dev, 70);
    
    ESP_LOGI(AUDIO_TAG, "Audio codec initialized successfully");
    return ESP_OK;
}

/**
 * Initialize microphone for voice input
 */
esp_err_t mic_init(void)
{
    ESP_LOGI(AUDIO_TAG, "Initializing microphone...");
    
    // Get microphone codec
    esp_codec_dev_handle_t mic_dev = bsp_audio_codec_microphone_init();
    if (mic_dev == NULL) {
        ESP_LOGE(AUDIO_TAG, "Failed to create microphone device");
        return ESP_FAIL;
    }
    
    // Open microphone
    esp_codec_dev_open(mic_dev, NULL);
    
    // Set microphone gain (0-100)
    esp_codec_dev_set_in_gain(mic_dev, 50);
    
    ESP_LOGI(AUDIO_TAG, "Microphone initialized successfully");
    return ESP_OK;
}

/**
 * Read audio data from microphone
 * @param buffer Buffer to store audio data
 * @param buffer_size Size of buffer in bytes
 * @return Number of bytes read
 */
size_t audio_read(int16_t *buffer, size_t buffer_size)
{
    if (codec_dev == NULL) {
        ESP_LOGE(AUDIO_TAG, "Codec not initialized");
        return 0;
    }
    
    size_t bytes_read = 0;
    esp_codec_dev_read(codec_dev, buffer, buffer_size, &bytes_read, portMAX_DELAY);
    
    return bytes_read;
}

/**
 * Simplified voice recognition initialization
 * Call this in your voice_recognition_task
 */
esp_err_t voice_recognition_init(void)
{
    esp_err_t ret;
    
    // Initialize audio
    ret = audio_init();
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Initialize microphone
    ret = mic_init();
    if (ret != ESP_OK) {
        return ret;
    }
    
    ESP_LOGI(AUDIO_TAG, "Voice recognition audio ready!");
    return ESP_OK;
}

#endif // AUDIO_HELPER_H
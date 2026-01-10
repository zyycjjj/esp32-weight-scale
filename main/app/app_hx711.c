#include "app_hx711.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include <math.h>
#include <string.h>

static const char *TAG = "app_hx711";

// 全局配置
static hx711_config_t g_config = {
    .offset = 0,
    .calibration_factor = 1.0f,
    .gain = HX711_GAIN_128,
    .stable_threshold = 2,
    .stable_samples = 5,
    .weight_threshold = 0.1f
};

// 重量稳定性缓冲区
static float weight_buffer[10] = {0};
static int buffer_index = 0;
static int sample_count = 0;

// 内部函数声明
static esp_err_t hx711_init_gpio(void);
static esp_err_t hx711_wait_ready(void);
static esp_err_t hx711_read_bytes(uint8_t *data, size_t len);
static esp_err_t hx711_set_gain_mode(int gain);

esp_err_t app_hx711_init(void)
{
    ESP_LOGI(TAG, "初始化HX711重量传感器");
    
    esp_err_t ret = hx711_init_gpio();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GPIO初始化失败");
        return ret;
    }
    
    // 设置增益
    ret = hx711_set_gain_mode(g_config.gain);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "设置增益失败");
        return ret;
    }
    
    // 零点校准
    ret = app_hx711_tare();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "零点校准失败，使用默认偏移值");
    }
    
    ESP_LOGI(TAG, "HX711初始化完成");
    return ESP_OK;
}

static esp_err_t hx711_init_gpio(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << HX711_SCK_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        return ret;
    }
    
    io_conf.pin_bit_mask = (1ULL << HX711_DOUT_PIN);
    io_conf.mode = GPIO_MODE_INPUT;
    ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // 初始化时钟引脚为低电平
    gpio_set_level(HX711_SCK_PIN, 0);
    
    return ESP_OK;
}

static esp_err_t hx711_wait_ready(void)
{
    int timeout = 100;
    while (gpio_get_level(HX711_DOUT_PIN) != 0 && timeout-- > 0) {
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    
    if (timeout <= 0) {
        ESP_LOGE(TAG, "等待HX711就绪超时");
        return ESP_ERR_TIMEOUT;
    }
    
    return ESP_OK;
}

static esp_err_t hx711_read_bytes(uint8_t *data, size_t len)
{
    esp_err_t ret = hx711_wait_ready();
    if (ret != ESP_OK) {
        return ret;
    }
    
    // 读取数据
    for (size_t i = 0; i < len; i++) {
        uint8_t byte = 0;
        for (int j = 0; j < 8; j++) {
            gpio_set_level(HX711_SCK_PIN, 1);
            esp_rom_delay_us(1);
            gpio_set_level(HX711_SCK_PIN, 0);
            esp_rom_delay_us(1);
            
            byte = (byte << 1) | gpio_get_level(HX711_DOUT_PIN);
        }
        data[i] = byte;
    }
    
    // 设置增益和时钟脉冲
    for (int i = 0; i < g_config.gain; i++) {
        gpio_set_level(HX711_SCK_PIN, 1);
        esp_rom_delay_us(1);
        gpio_set_level(HX711_SCK_PIN, 0);
        esp_rom_delay_us(1);
    }
    
    return ESP_OK;
}

static esp_err_t hx711_set_gain_mode(int gain)
{
    uint8_t data[3];
    esp_err_t ret = hx711_read_bytes(data, 3);
    if (ret == ESP_OK) {
        g_config.gain = gain;
        ESP_LOGI(TAG, "设置增益: %d", gain);
    }
    return ret;
}

esp_err_t app_hx711_read(hx711_data_t *data)
{
    if (!data) {
        return ESP_ERR_INVALID_ARG;
    }
    
    uint8_t bytes[3];
    esp_err_t ret = hx711_read_bytes(bytes, 3);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // 组合24位数据
    int32_t raw_value = ((int32_t)bytes[0] << 16) | ((int32_t)bytes[1] << 8) | bytes[2];
    
    // 符号扩展
    if (raw_value & 0x800000) {
        raw_value |= 0xFF000000;
    }
    
    // 减去偏移值
    raw_value -= g_config.offset;
    
    // 转换为重量
    data->weight = (float)raw_value / g_config.calibration_factor;
    data->raw_value = raw_value;
    data->timestamp = xTaskGetTickCount();
    data->calibration_factor = g_config.calibration_factor;
    
    // 检查重量稳定性
    data->stable = app_hx711_is_weight_stable(data->weight);
    
    // 更新稳定性缓冲区
    weight_buffer[buffer_index] = data->weight;
    buffer_index = (buffer_index + 1) % 10;
    if (sample_count < 10) {
        sample_count++;
    }
    
    ESP_LOGD(TAG, "读取重量: %.2f kg (原始值: %ld)", data->weight, raw_value);
    
    return ESP_OK;
}

esp_err_t app_hx711_set_calibration(float calibration_factor, int32_t offset)
{
    if (calibration_factor == 0.0f) {
        return ESP_ERR_INVALID_ARG;
    }
    
    g_config.calibration_factor = calibration_factor;
    g_config.offset = offset;
    
    ESP_LOGI(TAG, "设置校准参数: 系数=%.2f, 偏移=%ld", calibration_factor, offset);
    return ESP_OK;
}

esp_err_t app_hx711_get_calibration(float *calibration_factor, int32_t *offset)
{
    if (!calibration_factor || !offset) {
        return ESP_ERR_INVALID_ARG;
    }
    
    *calibration_factor = g_config.calibration_factor;
    *offset = g_config.offset;
    
    return ESP_OK;
}

esp_err_t app_hx711_tare(void)
{
    ESP_LOGI(TAG, "执行零点校准");
    
    const int samples = 10;
    int64_t sum = 0;
    
    for (int i = 0; i < samples; i++) {
        uint8_t bytes[3];
        esp_err_t ret = hx711_read_bytes(bytes, 3);
        if (ret != ESP_OK) {
            return ret;
        }
        
        int32_t raw_value = ((int32_t)bytes[0] << 16) | ((int32_t)bytes[1] << 8) | bytes[2];
        if (raw_value & 0x800000) {
            raw_value |= 0xFF000000;
        }
        
        sum += raw_value;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    
    g_config.offset = (int32_t)(sum / samples);
    
    // 清空稳定性缓冲区
    memset(weight_buffer, 0, sizeof(weight_buffer));
    buffer_index = 0;
    sample_count = 0;
    
    ESP_LOGI(TAG, "零点校准完成，偏移值: %ld", g_config.offset);
    return ESP_OK;
}

esp_err_t app_hx711_set_gain(int gain)
{
    if (gain != HX711_GAIN_128 && gain != HX711_GAIN_64 && gain != HX711_GAIN_32) {
        return ESP_ERR_INVALID_ARG;
    }
    
    return hx711_set_gain_mode(gain);
}

bool app_hx711_is_weight_stable(float weight)
{
    if (sample_count < g_config.stable_samples) {
        return false;
    }
    
    // 计算最后几个样本的标准差
    float sum = 0;
    for (int i = 0; i < g_config.stable_samples; i++) {
        int idx = (buffer_index - g_config.stable_samples + i + 10) % 10;
        sum += weight_buffer[idx];
    }
    float mean = sum / g_config.stable_samples;
    
    float variance = 0;
    for (int i = 0; i < g_config.stable_samples; i++) {
        int idx = (buffer_index - g_config.stable_samples + i + 10) % 10;
        variance += (weight_buffer[idx] - mean) * (weight_buffer[idx] - mean);
    }
    float std_dev = sqrt(variance / g_config.stable_samples);
    
    return std_dev <= g_config.stable_threshold;
}

esp_err_t app_hx711_read_raw(int32_t *raw_value)
{
    if (!raw_value) {
        return ESP_ERR_INVALID_ARG;
    }
    
    uint8_t bytes[3];
    esp_err_t ret = hx711_read_bytes(bytes, 3);
    if (ret != ESP_OK) {
        return ret;
    }
    
    int32_t value = ((int32_t)bytes[0] << 16) | ((int32_t)bytes[1] << 8) | bytes[2];
    if (value & 0x800000) {
        value |= 0xFF000000;
    }
    
    *raw_value = value;
    return ESP_OK;
}

esp_err_t app_hx711_set_config(hx711_config_t config)
{
    if (config.calibration_factor == 0.0f) {
        return ESP_ERR_INVALID_ARG;
    }
    
    g_config = config;
    
    // 重新设置增益
    esp_err_t ret = hx711_set_gain_mode(config.gain);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "设置增益失败");
    }
    
    ESP_LOGI(TAG, "配置更新完成");
    return ESP_OK;
}

esp_err_t app_hx711_get_config(hx711_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    
    *config = g_config;
    return ESP_OK;
}

void app_hx711_deinit(void)
{
    ESP_LOGI(TAG, "HX711去初始化");
    
    // 重置GPIO
    gpio_reset_pin(HX711_SCK_PIN);
    gpio_reset_pin(HX711_DOUT_PIN);
    
    // 清空缓冲区
    memset(weight_buffer, 0, sizeof(weight_buffer));
    buffer_index = 0;
    sample_count = 0;
}
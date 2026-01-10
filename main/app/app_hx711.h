#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

// HX711 引脚定义
#define HX711_DOUT_PIN     GPIO_NUM_4    // HX711数据引脚
#define HX711_SCK_PIN      GPIO_NUM_5    // HX711时钟引脚

// HX711 配置
#define HX711_GAIN_128      1    // 增益 128
#define HX711_GAIN_64       3    // 增益 64
#define HX711_GAIN_32       2    // 增益 32

// HX711 数据结构
typedef struct {
    float weight;          // 重量值(kg)
    bool stable;           // 重量是否稳定
    uint32_t timestamp;    // 读取时间戳
    int32_t raw_value;     // 原始ADC值
    float calibration_factor; // 校准系数
} hx711_data_t;

// HX711 配置结构
typedef struct {
    int32_t offset;        // 零点偏移值
    float calibration_factor; // 校准系数
    int gain;              // 增益设置
    int stable_threshold;  // 稳定性阈值
    int stable_samples;    // 稳定样本数
    float weight_threshold; // 重量检测阈值
} hx711_config_t;

// 初始化HX711
esp_err_t app_hx711_init(void);

// 读取HX711数据
esp_err_t app_hx711_read(hx711_data_t *data);

// 设置校准参数
esp_err_t app_hx711_set_calibration(float calibration_factor, int32_t offset);

// 获取校准参数
esp_err_t app_hx711_get_calibration(float *calibration_factor, int32_t *offset);

// 零点校准
esp_err_t app_hx711_tare(void);

// 设置增益
esp_err_t app_hx711_set_gain(int gain);

// 检查重量是否稳定
bool app_hx711_is_weight_stable(float weight);

// 获取原始ADC值
esp_err_t app_hx711_read_raw(int32_t *raw_value);

// 设置配置参数
esp_err_t app_hx711_set_config(hx711_config_t config);

// 获取配置参数
esp_err_t app_hx711_get_config(hx711_config_t *config);

// 去初始化HX711
void app_hx711_deinit(void);

#ifdef __cplusplus
}
#endif
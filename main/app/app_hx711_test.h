/**
 * @file app_hx711_test.h
 * @brief HX711重量传感器测试模块头文件
 * @author AI Assistant
 * @date 2026-01-10
 */

#ifndef APP_HX711_TEST_H
#define APP_HX711_TEST_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 测试HX711基本功能
 */
void app_hx711_test_basic(void);

/**
 * @brief 测试HX711校准功能
 */
void app_hx711_test_calibration(void);

/**
 * @brief 启动HX711测试
 * @return ESP_OK 成功
 * @return ESP_FAIL 失败
 */
esp_err_t app_hx711_test_start(void);

#ifdef __cplusplus
}
#endif

#endif // APP_HX711_TEST_H
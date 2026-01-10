/**
 * @file ui_welcome.h
 * @brief 欢迎页面UI模块头文件
 * @author AI Assistant
 * @date 2026-01-10
 */

#ifndef UI_WELCOME_H
#define UI_WELCOME_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 欢迎页面状态
 */
typedef enum {
    UI_WELCOME_STATE_INIT,           // 初始化状态
    UI_WELCOME_STATE_WIFI_CONNECTING, // WiFi连接中
    UI_WELCOME_STATE_WIFI_CONNECTED,  // WiFi已连接
    UI_WELCOME_STATE_READY           // 准备就绪
} ui_welcome_state_t;

/**
 * @brief 初始化欢迎页面
 * @return ESP_OK 成功
 * @return ESP_FAIL 失败
 */
esp_err_t ui_welcome_init(void);

/**
 * @brief 显示欢迎页面
 * @return ESP_OK 成功
 * @return ESP_FAIL 失败
 */
esp_err_t ui_welcome_show(void);

/**
 * @brief 隐藏欢迎页面
 * @return ESP_OK 成功
 * @return ESP_FAIL 失败
 */
esp_err_t ui_welcome_hide(void);

/**
 * @brief 更新欢迎页面状态
 * @param state 当前状态
 * @return ESP_OK 成功
 * @return ESP_FAIL 失败
 */
esp_err_t ui_welcome_update_state(ui_welcome_state_t state);

/**
 * @brief 设置WiFi连接状态
 * @param connected 是否连接
 * @return ESP_OK 成功
 * @return ESP_FAIL 失败
 */
esp_err_t ui_welcome_set_wifi_status(bool connected);

/**
 * @brief 设置进度百分比
 * @param percentage 进度百分比 (0-100)
 * @return ESP_OK 成功
 * @return ESP_FAIL 失败
 */
esp_err_t ui_welcome_set_progress(uint8_t percentage);

/**
 * @brief 设置状态文本
 * @param text 状态文本
 * @return ESP_OK 成功
 * @return ESP_FAIL 失败
 */
esp_err_t ui_welcome_set_status_text(const char *text);

/**
 * @brief 获取欢迎页面状态
 * @return 当前状态
 */
ui_welcome_state_t ui_welcome_get_state(void);

/**
 * @brief 销毁欢迎页面
 * @return ESP_OK 成功
 * @return ESP_FAIL 失败
 */
esp_err_t ui_welcome_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // UI_WELCOME_H
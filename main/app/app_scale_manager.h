#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "app_wechat.h"

#ifdef __cplusplus
extern "C" {
#endif

// 体重秤状态
typedef enum {
    SCALE_STATE_INIT = 0,           // 初始化状态
    SCALE_STATE_WIFI_CONNECTING,    // WiFi连接中
    SCALE_STATE_WAITING_WEIGHT,     // 等待测量重量
    SCALE_STATE_MEASURING,          // 测量中
    SCALE_STATE_WEIGHT_STABLE,      // 重量稳定
    SCALE_STATE_GENERATING_PAYMENT, // 生成支付中
    SCALE_STATE_WAITING_PAYMENT,    // 等待支付
    SCALE_STATE_PAYMENT_SUCCESS,    // 支付成功
    SCALE_STATE_PAYMENT_FAILED,     // 支付失败
    SCALE_STATE_TIMEOUT,            // 超时
    SCALE_STATE_ERROR               // 错误状态
} scale_state_t;

// 体重秤配置
typedef struct {
    float stable_threshold;         // 稳定阈值(kg)
    int stable_samples;             // 稳定样本数
    int payment_timeout_seconds;    // 支付超时时间
    float minimum_weight;           // 最小测量重量
    float maximum_weight;           // 最大测量重量
} scale_config_t;

// 初始化体重秤管理器
esp_err_t app_scale_manager_init(void);

// 启动体重秤管理任务
esp_err_t app_scale_manager_start(void);

// 停止体重秤管理任务
esp_err_t app_scale_manager_stop(void);

// 获取当前状态
scale_state_t app_scale_get_state(void);

// 手动设置状态
esp_err_t app_scale_set_state(scale_state_t state);

// 获取当前配置
scale_config_t app_scale_get_config(void);

// 设置配置
esp_err_t app_scale_set_config(scale_config_t config);

// 重量测量完成回调函数类型定义
typedef void (*weight_measure_callback_t)(float weight);

// 支付状态变化回调函数类型定义
typedef void (*payment_status_callback_t)(wechat_payment_status_t status);

// 注册回调函数
esp_err_t app_scale_register_weight_callback(weight_measure_callback_t callback);
esp_err_t app_scale_register_payment_callback(payment_status_callback_t callback);

#ifdef __cplusplus
}
#endif
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// 微信支付状态枚举
typedef enum {
    WECHAT_STATUS_PENDING = 0,        // 等待支付
    WECHAT_STATUS_PAID,               // 已支付
    WECHAT_STATUS_FAILED,             // 支付失败
    WECHAT_STATUS_TIMEOUT,            // 支付超时
    WECHAT_STATUS_CANCELLED,          // 支付取消
    WECHAT_STATUS_ERROR               // 错误
} wechat_payment_status_t;

// 微信支付信息结构
typedef struct {
    char *qr_code_url;        // 二维码URL
    char *order_id;           // 订单ID
    float amount;             // 支付金额
    int timeout_seconds;      // 支付超时时间
    bool payment_completed;   // 支付是否完成
} wechat_payment_info_t;

// 支付请求参数
typedef struct {
    float weight;                    // 重量(kg)
    float price_per_kg;              // 每公斤价格
    float total_amount;              // 总金额
    char *description;               // 支付描述
    char *user_id;                   // 用户ID
    int timeout_seconds;             // 支付超时时间
} wechat_payment_request_t;

// 服务器响应结构
typedef struct {
    bool success;                    // 请求是否成功
    char *error_message;             // 错误信息
    char *order_id;                  // 订单ID
    char *payment_url;               // 支付URL
    char *qr_code_data;              // 二维码数据
    int timeout_seconds;             // 超时时间
} wechat_server_response_t;

// 初始化微信支付模块
esp_err_t app_wechat_init(void);

// 请求微信支付URL
esp_err_t app_wechat_request_payment_url(float weight, char **payment_url, char **order_id);

// 轮询支付状态
esp_err_t app_wechat_poll_payment_status(const char *order_id, wechat_payment_status_t *status);

// 生成二维码
esp_err_t app_wechat_generate_qrcode(const char *url, char **qr_code_data);

// 清理资源
void app_wechat_cleanup(void);

// 设置服务器URL
esp_err_t app_wechat_set_server_url(const char *url);

// 设置每公斤价格
esp_err_t app_wechat_set_price_per_kg(float price);

#ifdef __cplusplus
}
#endif
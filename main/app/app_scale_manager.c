#include "app_scale_manager.h"
#include "app_wifi.h"
#include "app_wechat.h"
#include "app_hx711.h"
#include "ui_main.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/timers.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "app_scale_manager";

// 默认配置
static scale_config_t g_default_config = {
    .stable_threshold = 0.1f,     // 0.1kg稳定阈值
    .stable_samples = 5,           // 连续5次稳定读数
    .payment_timeout_seconds = 300, // 5分钟支付超时
    .minimum_weight = 5.0f,        // 最小5kg
    .maximum_weight = 200.0f       // 最大200kg
};

// 全局变量
static scale_config_t g_config = g_default_config;
static scale_state_t g_current_state = SCALE_STATE_INIT;
static TaskHandle_t g_scale_task_handle = NULL;
static TimerHandle_t g_payment_timer = NULL;
static QueueHandle_t g_event_queue = NULL;

// 回调函数
static weight_measure_callback_t g_weight_callback = NULL;
static payment_status_callback_t g_payment_callback = NULL;

// 事件类型
typedef enum {
    EVENT_WIFI_CONNECTED = 0,
    EVENT_WIFI_DISCONNECTED,
    EVENT_WEIGHT_DETECTED,
    EVENT_WEIGHT_STABLE,
    EVENT_WEIGHT_REMOVED,
    EVENT_PAYMENT_REQUESTED,
    EVENT_PAYMENT_SUCCESS,
    EVENT_PAYMENT_FAILED,
    EVENT_PAYMENT_TIMEOUT,
    EVENT_ERROR,
    EVENT_RESET
} scale_event_t;

// 事件数据
typedef struct {
    scale_event_t event_type;
    float weight;
    char *order_id;
    wechat_payment_status_t payment_status;
} scale_event_data_t;

// 内部函数声明
static void scale_manager_task(void *arg);
static esp_err_t send_event(scale_event_t event_type, float weight, const char *order_id, wechat_payment_status_t payment_status);
static void payment_timeout_callback(TimerHandle_t timer);
static void handle_state_transition(scale_event_t event, const scale_event_data_t *event_data);
static bool is_weight_stable(float weight);
static void start_payment_timer(void);
static void stop_payment_timer(void);

esp_err_t app_scale_manager_init(void)
{
    ESP_LOGI(TAG, "初始化体重秤管理器");
    
    // 创建事件队列
    g_event_queue = xQueueCreate(10, sizeof(scale_event_data_t));
    if (!g_event_queue) {
        ESP_LOGE(TAG, "创建事件队列失败");
        return ESP_ERR_NO_MEM;
    }
    
    // 创建支付超时定时器
    g_payment_timer = xTimerCreate("payment_timer", 
                                   pdMS_TO_TICKS(g_config.payment_timeout_seconds * 1000),
                                   pdFALSE, // 单次触发
                                   (void *)0,
                                   payment_timeout_callback);
    if (!g_payment_timer) {
        ESP_LOGE(TAG, "创建支付定时器失败");
        vQueueDelete(g_event_queue);
        return ESP_ERR_NO_MEM;
    }
    
    g_current_state = SCALE_STATE_INIT;
    
    ESP_LOGI(TAG, "体重秤管理器初始化完成");
    return ESP_OK;
}

esp_err_t app_scale_manager_start(void)
{
    if (g_scale_task_handle) {
        ESP_LOGW(TAG, "体重秤任务已在运行");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "启动体重秤管理任务");
    
    BaseType_t ret = xTaskCreate(scale_manager_task, "scale_manager", 4096, NULL, 5, &g_scale_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "创建体重秤任务失败");
        return ESP_ERR_NO_MEM;
    }
    
    // 发送启动事件
    send_event(EVENT_WIFI_CONNECTED, 0.0f, NULL, WECHAT_STATUS_PENDING);
    
    return ESP_OK;
}

esp_err_t app_scale_manager_stop(void)
{
    if (!g_scale_task_handle) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "停止体重秤管理任务");
    
    vTaskDelete(g_scale_task_handle);
    g_scale_task_handle = NULL;
    
    stop_payment_timer();
    
    return ESP_OK;
}

static void scale_manager_task(void *arg)
{
    scale_event_data_t event_data;
    
    ESP_LOGI(TAG, "体重秤管理任务启动");
    
    while (true) {
        // 等待事件
        if (xQueueReceive(g_event_queue, &event_data, portMAX_DELAY) == pdTRUE) {
            ESP_LOGD(TAG, "收到事件: %d", event_data.event_type);
            
            // 处理状态转换
            handle_state_transition(event_data.event_type, &event_data);
        }
    }
    
    vTaskDelete(NULL);
}

static void handle_state_transition(scale_event_t event, const scale_event_data_t *event_data)
{
    scale_state_t previous_state = g_current_state;
    
    ESP_LOGD(TAG, "当前状态: %d, 事件: %d", g_current_state, event);
    
    switch (g_current_state) {
        case SCALE_STATE_INIT:
            if (event == EVENT_WIFI_CONNECTED) {
                g_current_state = SCALE_STATE_WAITING_WEIGHT;
                ESP_LOGI(TAG, "WiFi已连接，等待重量测量");
                ui_update_wifi_status(true);
            }
            break;
            
        case SCALE_STATE_WIFI_CONNECTING:
            if (event == EVENT_WIFI_CONNECTED) {
                g_current_state = SCALE_STATE_WAITING_WEIGHT;
                ESP_LOGI(TAG, "WiFi已连接，等待重量测量");
                ui_update_wifi_status(true);
            } else if (event == EVENT_WIFI_DISCONNECTED) {
                ESP_LOGE(TAG, "WiFi连接失败");
                ui_update_wifi_status(false);
            }
            break;
            
        case SCALE_STATE_WAITING_WEIGHT:
            if (event == EVENT_WEIGHT_DETECTED) {
                if (event_data->weight >= g_config.minimum_weight && 
                    event_data->weight <= g_config.maximum_weight) {
                    g_current_state = SCALE_STATE_MEASURING;
                    ESP_LOGI(TAG, "检测到重量: %.1f kg, 开始测量", event_data->weight);
                    ui_show_weight_page();
                    ui_update_weight(event_data->weight);
                } else {
                    ESP_LOGW(TAG, "重量超出范围: %.1f kg", event_data->weight);
                }
            }
            break;
            
        case SCALE_STATE_MEASURING:
            if (event == EVENT_WEIGHT_DETECTED) {
                ui_update_weight(event_data->weight);
                
                if (is_weight_stable(event_data->weight)) {
                    g_current_state = SCALE_STATE_WEIGHT_STABLE;
                    ESP_LOGI(TAG, "重量稳定: %.1f kg", event_data->weight);
                    
                    // 调用回调函数
                    if (g_weight_callback) {
                        g_weight_callback(event_data->weight);
                    }
                    
                    // 自动请求支付
                    send_event(EVENT_PAYMENT_REQUESTED, event_data->weight, NULL, WECHAT_STATUS_PENDING);
                }
            } else if (event == EVENT_WEIGHT_REMOVED) {
                g_current_state = SCALE_STATE_WAITING_WEIGHT;
                ESP_LOGI(TAG, "重量移除，返回等待状态");
            }
            break;
            
        case SCALE_STATE_WEIGHT_STABLE:
            if (event == EVENT_PAYMENT_REQUESTED) {
                g_current_state = SCALE_STATE_GENERATING_PAYMENT;
                ESP_LOGI(TAG, "请求生成支付二维码");
                
                // 请求支付URL
                char *payment_url = NULL;
                char *order_id = NULL;
                esp_err_t ret = app_wechat_request_payment_url(event_data->weight, &payment_url, &order_id);
                
                if (ret == ESP_OK && payment_url && order_id) {
                    g_current_state = SCALE_STATE_WAITING_PAYMENT;
                    ESP_LOGI(TAG, "支付二维码生成成功");
                    
                    // 显示支付页面
                    ui_show_payment_page(payment_url);
                    
                    // 启动支付超时定时器
                    start_payment_timer();
                    
                    // 开始轮询支付状态
                    // 这里可以创建一个单独的任务来轮询支付状态
                } else {
                    ESP_LOGE(TAG, "生成支付二维码失败");
                    g_current_state = SCALE_STATE_ERROR;
                }
                
                // 清理内存
                if (payment_url) free(payment_url);
                if (order_id) free(order_id);
            }
            break;
            
        case SCALE_STATE_WAITING_PAYMENT:
            if (event == EVENT_PAYMENT_SUCCESS) {
                stop_payment_timer();
                g_current_state = SCALE_STATE_PAYMENT_SUCCESS;
                ESP_LOGI(TAG, "支付成功");
                
                ui_show_success_page();
                
                // 调用回调函数
                if (g_payment_callback) {
                    g_payment_callback(WECHAT_STATUS_PAID);
                }
                
                // 5秒后重置到等待状态
                vTaskDelay(pdMS_TO_TICKS(5000));
                send_event(EVENT_RESET, 0.0f, NULL, WECHAT_STATUS_PENDING);
                
            } else if (event == EVENT_PAYMENT_FAILED || event == EVENT_PAYMENT_TIMEOUT) {
                stop_payment_timer();
                g_current_state = (event == EVENT_PAYMENT_TIMEOUT) ? SCALE_STATE_TIMEOUT : SCALE_STATE_PAYMENT_FAILED;
                ESP_LOGE(TAG, "支付失败或超时");
                
                ui_update_payment_status(event == EVENT_PAYMENT_TIMEOUT ? WECHAT_STATUS_TIMEOUT : WECHAT_STATUS_FAILED);
                
                // 调用回调函数
                if (g_payment_callback) {
                    g_payment_callback(event == EVENT_PAYMENT_TIMEOUT ? WECHAT_STATUS_TIMEOUT : WECHAT_STATUS_FAILED);
                }
                
                // 3秒后重置到等待状态
                vTaskDelay(pdMS_TO_TICKS(3000));
                send_event(EVENT_RESET, 0.0f, NULL, WECHAT_STATUS_PENDING);
            }
            break;
            
        case SCALE_STATE_PAYMENT_SUCCESS:
        case SCALE_STATE_PAYMENT_FAILED:
        case SCALE_STATE_TIMEOUT:
            if (event == EVENT_RESET) {
                g_current_state = SCALE_STATE_WAITING_WEIGHT;
                ESP_LOGI(TAG, "重置到等待重量状态");
                ui_show_weight_page();
            }
            break;
            
        case SCALE_STATE_ERROR:
            if (event == EVENT_RESET) {
                g_current_state = SCALE_STATE_WAITING_WEIGHT;
                ESP_LOGI(TAG, "从错误状态恢复");
                ui_show_weight_page();
            }
            break;
    }
    
    if (previous_state != g_current_state) {
        ESP_LOGI(TAG, "状态转换: %d -> %d", previous_state, g_current_state);
    }
}

static esp_err_t send_event(scale_event_t event_type, float weight, const char *order_id, wechat_payment_status_t payment_status)
{
    if (!g_event_queue) {
        return ESP_ERR_INVALID_STATE;
    }
    
    scale_event_data_t event_data = {
        .event_type = event_type,
        .weight = weight,
        .order_id = order_id ? strdup(order_id) : NULL,
        .payment_status = payment_status
    };
    
    if (xQueueSend(g_event_queue, &event_data, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "发送事件失败");
        if (event_data.order_id) {
            free(event_data.order_id);
        }
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

static bool is_weight_stable(float weight)
{
    static float weight_buffer[10] = {0};
    static int buffer_index = 0;
    static int sample_count = 0;
    
    weight_buffer[buffer_index] = weight;
    buffer_index = (buffer_index + 1) % 10;
    
    if (sample_count < 10) {
        sample_count++;
    }
    
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
    
    ESP_LOGD(TAG, "重量稳定性检查: mean=%.2f, std=%.2f", mean, std_dev);
    
    return std_dev <= g_config.stable_threshold;
}

static void payment_timeout_callback(TimerHandle_t timer)
{
    ESP_LOGW(TAG, "支付超时");
    send_event(EVENT_PAYMENT_TIMEOUT, 0.0f, NULL, WECHAT_STATUS_TIMEOUT);
}

static void start_payment_timer(void)
{
    if (g_payment_timer) {
        xTimerChangePeriod(g_payment_timer, 
                          pdMS_TO_TICKS(g_config.payment_timeout_seconds * 1000), 
                          pdMS_TO_TICKS(100));
        xTimerStart(g_payment_timer, pdMS_TO_TICKS(100));
    }
}

static void stop_payment_timer(void)
{
    if (g_payment_timer) {
        xTimerStop(g_payment_timer, pdMS_TO_TICKS(100));
    }
}

// 公共API实现
scale_state_t app_scale_get_state(void)
{
    return g_current_state;
}

esp_err_t app_scale_set_state(scale_state_t state)
{
    g_current_state = state;
    return ESP_OK;
}

scale_config_t app_scale_get_config(void)
{
    return g_config;
}

esp_err_t app_scale_set_config(scale_config_t config)
{
    g_config = config;
    return ESP_OK;
}

esp_err_t app_scale_register_weight_callback(weight_measure_callback_t callback)
{
    g_weight_callback = callback;
    return ESP_OK;
}

esp_err_t app_scale_register_payment_callback(payment_status_callback_t callback)
{
    g_payment_callback = callback;
    return ESP_OK;
}
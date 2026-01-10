#include <stdio.h>
#include <math.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "bsp_storage.h"
#include "settings.h"
#include "app_led.h"
#include "app_wifi.h"
#include "app_wechat.h"
#include "app_hx711.h"
#include "app_scale_manager.h"
#include "gui/ui_main.h"
#include "gui/ui_welcome.h"

#include "bsp_board.h"
#include "bsp/esp-bsp.h"

static const char *TAG = "main";

// 重量监测任务
static void weight_monitor_task(void *arg)
{
    float weight = 0.0f;
    float last_weight = 0.0f;
    bool weight_detected = false;
    
    ESP_LOGI(TAG, "重量监测任务启动");
    
    while (true) {
        esp_err_t ret = app_hx711_read_weight(&weight);
        
        if (ret == ESP_OK) {
            ESP_LOGD(TAG, "读取重量: %.2f kg", weight);
            
            // 检测重量变化
            if (!weight_detected && weight > 5.0f) {
                weight_detected = true;
                ESP_LOGI(TAG, "检测到重量: %.1f kg", weight);
                ui_update_weight(weight);
            } else if (weight_detected && weight <= 2.0f) {
                weight_detected = false;
                ESP_LOGI(TAG, "重量移除");
            } else if (weight_detected) {
                // 更新重量显示
                ui_update_weight(weight);
            }
            
            last_weight = weight;
        } else {
            ESP_LOGW(TAG, "读取重量失败");
        }
        
        vTaskDelay(pdMS_TO_TICKS(500)); // 每500ms读取一次
    }
    
    vTaskDelete(NULL);
}

// WiFi事件处理
static void wifi_event_handler(bool connected)
{
    ESP_LOGI(TAG, "WiFi状态变化: %s", connected ? "已连接" : "断开");
    ui_update_wifi_status(connected);
}

// 重量测量完成回调
static void weight_measure_callback(float weight)
{
    ESP_LOGI(TAG, "重量测量完成: %.1f kg", weight);
}

// 支付状态变化回调
static void payment_status_callback(wechat_payment_status_t status)
{
    ESP_LOGI(TAG, "支付状态变化: %d", status);
    ui_update_payment_status(status);
}

static esp_err_t audio_mute_function(AUDIO_PLAYER_MUTE_SETTING setting)
{
    return ESP_OK;
}

void app_main(void)
{
    ESP_LOGI(TAG, "智能体重秤系统启动 - Compile time: %s %s", __DATE__, __TIME__);
    
    /* Initialize NVS. */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(settings_read_parameter_from_nvs());

    bsp_spiffs_mount();
    bsp_i2c_init();

    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_H_RES * CONFIG_BSP_LCD_DRAW_BUF_HEIGHT,
        .double_buffer = 0,
        .flags = {
            .buff_dma = true,
        }
    };
    cfg.lvgl_port_cfg.task_affinity = 1;
    bsp_display_start_with_config(&cfg);
    bsp_board_init();

    ESP_LOGI(TAG, "启动智能体重秤界面");
    ESP_ERROR_CHECK(ui_main_start());

    vTaskDelay(pdMS_TO_TICKS(500));
    bsp_display_backlight_on();

    // 初始化欢迎页面
    ESP_LOGI(TAG, "初始化欢迎页面");
    ui_welcome_init();
    ui_welcome_show();
    ui_welcome_update_state(UI_WELCOME_STATE_INIT);

    // 初始化WiFi
    ESP_LOGI(TAG, "初始化WiFi连接");
    ui_welcome_set_status_text("正在初始化WiFi...");
    ui_welcome_set_progress(20);
    app_wifi_init();
    ui_welcome_update_state(UI_WELCOME_STATE_WIFI_CONNECTING);
    
    // 启动WiFi连接
    app_wifi_start();
    
    // 等待WiFi连接
    while (!app_wifi_is_connected()) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    ui_welcome_update_state(UI_WELCOME_STATE_WIFI_CONNECTED);
    
    // 初始化HX711体重秤
    ESP_LOGI(TAG, "初始化HX711体重秤");
    ui_welcome_set_status_text("正在初始化体重秤...");
    ui_welcome_set_progress(40);
    app_hx711_init();
    
    // 初始化微信支付模块
    ESP_LOGI(TAG, "初始化微信支付模块");
    ui_welcome_set_status_text("正在初始化支付模块...");
    ui_welcome_set_progress(60);
    app_wechat_init();
    
    // 初始化体重秤管理器
    ESP_LOGI(TAG, "初始化体重秤管理器");
    ui_welcome_set_status_text("正在初始化系统管理器...");
    ui_welcome_set_progress(80);
    app_scale_manager_init();
    
    // 注册回调函数
    app_scale_register_weight_callback(weight_measure_callback);
    app_scale_register_payment_callback(payment_status_callback);
    
    // 启动体重秤管理器
    app_scale_manager_start();
    
    // 系统就绪
    ui_welcome_set_status_text("系统就绪");
    ui_welcome_set_progress(100);
    ui_welcome_update_state(UI_WELCOME_STATE_READY);
    
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // 隐藏欢迎页面，显示主界面
    ui_welcome_hide();
    
    // 启动重量监测任务
    xTaskCreate(weight_monitor_task, "weight_monitor", 2048, NULL, 5, NULL);
    
    ESP_LOGI(TAG, "智能体重秤系统启动完成");
}
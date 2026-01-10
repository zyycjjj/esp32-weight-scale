#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lvgl.h"

#include "app_wifi.h"
#include "app_hx711.h"
#include "app_wechat.h"
#include "app_scale_manager.h"
#include "ui_main.h"

static const char *TAG = "app_main";

void app_main(void)
{
    ESP_LOGI(TAG, "智能体重秤启动");
    
    // 初始化NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // 初始化WiFi
    app_wifi_init();
    
    // 初始化HX711
    ret = app_hx711_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HX711初始化失败: %s", esp_err_to_name(ret));
    }
    
    // 初始化微信支付
    ret = app_wechat_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "微信支付初始化失败: %s", esp_err_to_name(ret));
    }
    
    // 初始化体重秤管理器
    ret = app_scale_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "体重秤管理器初始化失败: %s", esp_err_to_name(ret));
    }
    
    // 启动WiFi连接
    ret = app_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi启动失败: %s", esp_err_to_name(ret));
    }
    
    // 启动UI
    ret = ui_main_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UI启动失败: %s", esp_err_to_name(ret));
    }
    
    ESP_LOGI(TAG, "智能体重秤初始化完成");
    
    // 主循环
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        // 检查WiFi状态
        bool wifi_connected = app_wifi_is_connected();
        if (wifi_connected) {
            ESP_LOGD(TAG, "WiFi已连接");
        } else {
            ESP_LOGD(TAG, "WiFi未连接");
        }
        
        // 检查体重数据
        float weight = 0.0f;
        bool is_stable = false;
        ret = app_hx711_read_weight(&weight);
        if (ret == ESP_OK) {
            app_hx711_is_stable(&is_stable);
            ESP_LOGD(TAG, "重量: %.2f kg, 稳定性: %s", weight, is_stable ? "稳定" : "不稳定");
        }
    }
}
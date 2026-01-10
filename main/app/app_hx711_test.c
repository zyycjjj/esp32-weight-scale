/**
 * @file app_hx711_test.c
 * @brief HX711重量传感器测试模块
 * @author AI Assistant
 * @date 2026-01-10
 */

#include "app_hx711.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "HX711_TEST";

/**
 * @brief 测试HX711基本功能
 */
void app_hx711_test_basic(void)
{
    ESP_LOGI(TAG, "开始HX711基本功能测试");
    
    // 初始化HX711
    esp_err_t ret = app_hx711_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HX711初始化失败: %s", esp_err_to_name(ret));
        return;
    }
    
    ESP_LOGI(TAG, "HX711初始化成功");
    
    // 测试读取原始数据
    int32_t raw_data = 0;
    for (int i = 0; i < 10; i++) {
        ret = app_hx711_read_raw(&raw_data);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "原始数据[%d]: %ld", i, raw_data);
        } else {
            ESP_LOGE(TAG, "读取原始数据失败: %s", esp_err_to_name(ret));
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    
    // 测试重量读取
    float weight = 0.0f;
    for (int i = 0; i < 10; i++) {
        ret = app_hx711_read_weight(&weight);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "重量数据[%d]: %.2f kg", i, weight);
        } else {
            ESP_LOGE(TAG, "读取重量失败: %s", esp_err_to_name(ret));
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
    // 测试去皮功能
    ESP_LOGI(TAG, "测试去皮功能");
    ret = app_hx711_tare();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "去皮成功");
    } else {
        ESP_LOGE(TAG, "去皮失败: %s", esp_err_to_name(ret));
    }
    
    // 测试稳定性检查
    ESP_LOGI(TAG, "测试重量稳定性检查");
    bool is_stable = false;
    for (int i = 0; i < 20; i++) {
        ret = app_hx711_is_stable(&is_stable);
        if (ret == ESP_OK) {
            ret = app_hx711_read_weight(&weight);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "重量: %.2f kg, 稳定性: %s", 
                         weight, is_stable ? "稳定" : "不稳定");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
    ESP_LOGI(TAG, "HX711基本功能测试完成");
}

/**
 * @brief 测试HX711校准功能
 */
void app_hx711_test_calibration(void)
{
    ESP_LOGI(TAG, "开始HX711校准测试");
    
    // 确保HX711已初始化
    esp_err_t ret = app_hx711_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HX711初始化失败: %s", esp_err_to_name(ret));
        return;
    }
    
    // 步骤1: 零点校准
    ESP_LOGI(TAG, "步骤1: 请确保秤上无物体，按任意键继续零点校准");
    vTaskDelay(pdMS_TO_TICKS(5000));  // 等待5秒
    
    ret = app_hx711_tare();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "零点校准完成");
    } else {
        ESP_LOGE(TAG, "零点校准失败: %s", esp_err_to_name(ret));
        return;
    }
    
    // 步骤2: 已知重量校准
    ESP_LOGI(TAG, "步骤2: 请放置已知重量的物体（建议1kg），按任意键继续校准");
    vTaskDelay(pdMS_TO_TICKS(10000));  // 等待10秒
    
    // 读取当前值
    float current_weight = 0.0f;
    ret = app_hx711_read_weight(&current_weight);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "当前读取重量: %.2f kg", current_weight);
        
        // 这里需要手动输入已知重量进行校准
        // 实际使用时需要通过UI输入
        float known_weight = 1.0f;  // 假设已知重量为1kg
        
        ESP_LOGI(TAG, "已知重量: %.2f kg", known_weight);
        ESP_LOGI(TAG, "校准系数需要根据实际值计算");
        
        // 简单的校准系数计算（实际应用中需要更精确的算法）
        float calibration_factor = known_weight / current_weight;
        ESP_LOGI(TAG, "建议校准系数: %.6f", calibration_factor);
        
    } else {
        ESP_LOGE(TAG, "读取重量失败: %s", esp_err_to_name(ret));
    }
    
    ESP_LOGI(TAG, "HX711校准测试完成");
}

/**
 * @brief HX711测试任务
 */
void hx711_test_task(void *parameter)
{
    ESP_LOGI(TAG, "HX711测试任务启动");
    
    // 等待系统稳定
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // 运行基本功能测试
    app_hx711_test_basic();
    
    // 等待一段时间
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    // 运行校准测试
    app_hx711_test_calibration();
    
    // 持续监控重量
    ESP_LOGI(TAG, "开始持续监控重量");
    while (1) {
        float weight = 0.0f;
        bool is_stable = false;
        
        esp_err_t ret = app_hx711_read_weight(&weight);
        if (ret == ESP_OK) {
            app_hx711_is_stable(&is_stable);
            ESP_LOGI(TAG, "重量: %.2f kg, 稳定性: %s", 
                     weight, is_stable ? "稳定" : "不稳定");
        }
        
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

/**
 * @brief 启动HX711测试
 */
esp_err_t app_hx711_test_start(void)
{
    BaseType_t ret = xTaskCreate(
        hx711_test_task,
        "hx711_test",
        4096,
        NULL,
        5,
        NULL
    );
    
    if (ret == pdPASS) {
        ESP_LOGI(TAG, "HX711测试任务创建成功");
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "HX711测试任务创建失败");
        return ESP_FAIL;
    }
}
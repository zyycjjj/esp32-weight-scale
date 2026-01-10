/**
 * @file ui_welcome.c
 * @brief 欢迎页面UI模块实现
 * @author AI Assistant
 * @date 2026-01-10
 */

#include "ui_welcome.h"
#include "ui_main.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

static const char *TAG = "ui_welcome";

// 欢迎页面UI元素
static lv_obj_t *s_welcome_page = NULL;
static lv_obj_t *s_title_label = NULL;
static lv_obj_t *s_subtitle_label = NULL;
static lv_obj_t *s_status_label = NULL;
static lv_obj_t *s_progress_bar = NULL;
static lv_obj_t *s_wifi_icon = NULL;
static lv_obj_t *s_logo_img = NULL;

// 状态变量
static ui_welcome_state_t s_current_state = UI_WELCOME_STATE_INIT;
static uint8_t s_progress = 0;
static char s_status_text[128] = "";

/**
 * @brief 创建欢迎页面UI元素
 */
static esp_err_t ui_welcome_create_elements(void)
{
    if (s_welcome_page) {
        ESP_LOGW(TAG, "欢迎页面已存在");
        return ESP_OK;
    }

    // 创建主容器
    s_welcome_page = lv_obj_create(lv_scr_act());
    if (!s_welcome_page) {
        ESP_LOGE(TAG, "创建欢迎页面失败");
        return ESP_FAIL;
    }

    // 设置页面样式
    lv_obj_set_size(s_welcome_page, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(s_welcome_page, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_border_width(s_welcome_page, 0, 0);
    lv_obj_set_style_pad_all(s_welcome_page, 0, 0);
    lv_obj_center(s_welcome_page);

    // 创建Logo区域
    s_logo_img = lv_img_create(s_welcome_page);
    lv_img_set_src(s_logo_img, LV_SYMBOL_WIFI);
    lv_obj_set_style_img_recolor(s_logo_img, lv_color_hex(0x00ff00), 0);
    lv_obj_align(s_logo_img, LV_ALIGN_TOP_MID, 0, 40);

    // 创建标题标签
    s_title_label = lv_label_create(s_welcome_page);
    if (!s_title_label) {
        ESP_LOGE(TAG, "创建标题标签失败");
        return ESP_FAIL;
    }
    lv_label_set_text(s_title_label, "智能体重秤");
    lv_obj_set_style_text_font(s_title_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_title_label, lv_color_hex(0xffffff), 0);
    lv_obj_align(s_title_label, LV_ALIGN_TOP_MID, 0, 120);

    // 创建副标题标签
    s_subtitle_label = lv_label_create(s_welcome_page);
    if (!s_subtitle_label) {
        ESP_LOGE(TAG, "创建副标题标签失败");
        return ESP_FAIL;
    }
    lv_label_set_text(s_subtitle_label, "智能健康，从这里开始");
    lv_obj_set_style_text_font(s_subtitle_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_subtitle_label, lv_color_hex(0xcccccc), 0);
    lv_obj_align(s_subtitle_label, LV_ALIGN_TOP_MID, 0, 160);

    // 创建WiFi图标
    s_wifi_icon = lv_label_create(s_welcome_page);
    if (!s_wifi_icon) {
        ESP_LOGE(TAG, "创建WiFi图标失败");
        return ESP_FAIL;
    }
    lv_label_set_text(s_wifi_icon, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(s_wifi_icon, lv_color_hex(0xff0000), 0);
    lv_obj_align(s_wifi_icon, LV_ALIGN_CENTER, -60, 0);

    // 创建进度条
    s_progress_bar = lv_bar_create(s_welcome_page);
    if (!s_progress_bar) {
        ESP_LOGE(TAG, "创建进度条失败");
        return ESP_FAIL;
    }
    lv_obj_set_size(s_progress_bar, 200, 10);
    lv_obj_align(s_progress_bar, LV_ALIGN_CENTER, 0, 40);
    lv_bar_set_range(s_progress_bar, 0, 100);
    lv_bar_set_value(s_progress_bar, 0, LV_ANIM_OFF);

    // 创建状态标签
    s_status_label = lv_label_create(s_welcome_page);
    if (!s_status_label) {
        ESP_LOGE(TAG, "创建状态标签失败");
        return ESP_FAIL;
    }
    lv_label_set_text(s_status_label, "正在初始化...");
    lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(0x999999), 0);
    lv_obj_align(s_status_label, LV_ALIGN_BOTTOM_MID, 0, -40);

    ESP_LOGI(TAG, "欢迎页面UI元素创建完成");
    return ESP_OK;
}

/**
 * @brief 更新WiFi图标显示
 */
static void ui_welcome_update_wifi_icon(void)
{
    if (!s_wifi_icon) {
        return;
    }

    switch (s_current_state) {
        case UI_WELCOME_STATE_INIT:
            lv_label_set_text(s_wifi_icon, LV_SYMBOL_WIFI);
            lv_obj_set_style_text_color(s_wifi_icon, lv_color_hex(0x999999), 0);
            break;
        case UI_WELCOME_STATE_WIFI_CONNECTING:
            lv_label_set_text(s_wifi_icon, LV_SYMBOL_WIFI);
            lv_obj_set_style_text_color(s_wifi_icon, lv_color_hex(0xff9900), 0);
            break;
        case UI_WELCOME_STATE_WIFI_CONNECTED:
            lv_label_set_text(s_wifi_icon, LV_SYMBOL_WIFI);
            lv_obj_set_style_text_color(s_wifi_icon, lv_color_hex(0x00ff00), 0);
            break;
        case UI_WELCOME_STATE_READY:
            lv_label_set_text(s_wifi_icon, LV_SYMBOL_OK);
            lv_obj_set_style_text_color(s_wifi_icon, lv_color_hex(0x00ff00), 0);
            break;
    }
}

esp_err_t ui_welcome_init(void)
{
    ESP_LOGI(TAG, "初始化欢迎页面");
    
    // 重置状态
    s_current_state = UI_WELCOME_STATE_INIT;
    s_progress = 0;
    strcpy(s_status_text, "正在初始化...");
    
    return ESP_OK;
}

esp_err_t ui_welcome_show(void)
{
    ESP_LOGI(TAG, "显示欢迎页面");
    
    esp_err_t ret = ui_welcome_create_elements();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "创建欢迎页面失败");
        return ret;
    }

    // 初始状态显示
    ui_welcome_update_wifi_icon();
    lv_label_set_text(s_status_label, s_status_text);
    lv_bar_set_value(s_progress_bar, s_progress, LV_ANIM_OFF);

    return ESP_OK;
}

esp_err_t ui_welcome_hide(void)
{
    ESP_LOGI(TAG, "隐藏欢迎页面");
    
    if (s_welcome_page) {
        lv_obj_del(s_welcome_page);
        s_welcome_page = NULL;
        s_title_label = NULL;
        s_subtitle_label = NULL;
        s_status_label = NULL;
        s_progress_bar = NULL;
        s_wifi_icon = NULL;
        s_logo_img = NULL;
    }

    return ESP_OK;
}

esp_err_t ui_welcome_update_state(ui_welcome_state_t state)
{
    if (s_current_state == state) {
        return ESP_OK; // 状态未变化
    }

    s_current_state = state;
    
    ESP_LOGI(TAG, "更新欢迎页面状态: %d", state);
    
    switch (state) {
        case UI_WELCOME_STATE_INIT:
            ui_welcome_set_status_text("正在初始化...");
            ui_welcome_set_progress(10);
            break;
        case UI_WELCOME_STATE_WIFI_CONNECTING:
            ui_welcome_set_status_text("正在连接WiFi...");
            ui_welcome_set_progress(30);
            break;
        case UI_WELCOME_STATE_WIFI_CONNECTED:
            ui_welcome_set_status_text("WiFi已连接");
            ui_welcome_set_progress(70);
            break;
        case UI_WELCOME_STATE_READY:
            ui_welcome_set_status_text("系统就绪");
            ui_welcome_set_progress(100);
            break;
    }

    ui_welcome_update_wifi_icon();
    return ESP_OK;
}

esp_err_t ui_welcome_set_wifi_status(bool connected)
{
    if (connected) {
        ui_welcome_update_state(UI_WELCOME_STATE_WIFI_CONNECTED);
    } else {
        ui_welcome_update_state(UI_WELCOME_STATE_WIFI_CONNECTING);
    }
    
    return ESP_OK;
}

esp_err_t ui_welcome_set_progress(uint8_t percentage)
{
    if (percentage > 100) {
        percentage = 100;
    }
    
    s_progress = percentage;
    
    if (s_progress_bar) {
        lv_bar_set_value(s_progress_bar, percentage, LV_ANIM_ON);
    }
    
    return ESP_OK;
}

esp_err_t ui_welcome_set_status_text(const char *text)
{
    if (!text) {
        return ESP_ERR_INVALID_ARG;
    }
    
    strncpy(s_status_text, text, sizeof(s_status_text) - 1);
    s_status_text[sizeof(s_status_text) - 1] = '\0';
    
    if (s_status_label) {
        lv_label_set_text(s_status_label, s_status_text);
    }
    
    return ESP_OK;
}

ui_welcome_state_t ui_welcome_get_state(void)
{
    return s_current_state;
}

esp_err_t ui_welcome_deinit(void)
{
    ESP_LOGI(TAG, "销毁欢迎页面");
    return ui_welcome_hide();
}
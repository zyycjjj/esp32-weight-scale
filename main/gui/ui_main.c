/*
 * SPDX-FileCopyrightText: 2015-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_check.h"
#include "bsp_board.h"
#include "bsp/esp-bsp.h"
#include "lvgl.h"
#include "app_wifi.h"
#include "app_wechat.h"
#include "app_hx711.h"
#include "settings.h"
#include "ui_main.h"

static const char *TAG = "ui_main";

LV_FONT_DECLARE(font_icon_16);

static lv_obj_t *g_main_screen = NULL;
static lv_obj_t *g_welcome_page = NULL;
static lv_obj_t *g_weight_page = NULL;
static lv_obj_t *g_payment_page = NULL;
static lv_obj_t *g_success_page = NULL;

static lv_obj_t *g_wifi_label = NULL;
static lv_obj_t *g_weight_label = NULL;
static lv_obj_t *g_qr_code = NULL;
static lv_obj_t *g_status_label = NULL;

static hx711_data_t g_current_weight;
static wechat_payment_info_t g_payment_info;

/* Creates a semaphore to handle concurrent call to lvgl stuff
 * If you wish to call *any* lvgl function from other threads/tasks
 * you should lock on the very same semaphore! */
SemaphoreHandle_t g_guisemaphore;

static void ui_create_welcome_page(void)
{
    g_welcome_page = lv_obj_create(g_main_screen);
    lv_obj_set_size(g_welcome_page, BSP_LCD_H_RES, BSP_LCD_V_RES);
    lv_obj_clear_flag(g_welcome_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(g_welcome_page, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_border_width(g_welcome_page, 0, 0);

    // 标题
    lv_obj_t *title = lv_label_create(g_welcome_page);
    lv_label_set_text(title, "智能体重秤");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 60);

    // WiFi状态
    g_wifi_label = lv_label_create(g_welcome_page);
    lv_label_set_text(g_wifi_label, "正在连接WiFi...");
    lv_obj_set_style_text_font(g_wifi_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(g_wifi_label, lv_color_hex(0x888888), 0);
    lv_obj_align(g_wifi_label, LV_ALIGN_CENTER, 0, -20);

    // 状态信息
    lv_obj_t *info = lv_label_create(g_welcome_page);
    lv_label_set_text(info, "请站在体重秤上");
    lv_obj_set_style_text_font(info, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(info, lv_color_hex(0x666666), 0);
    lv_obj_align(info, LV_ALIGN_CENTER, 0, 20);

    // 版本信息
    lv_obj_t *version = lv_label_create(g_welcome_page);
    lv_label_set_text(version, "v1.0.0");
    lv_obj_set_style_text_font(version, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(version, lv_color_hex(0x444444), 0);
    lv_obj_align(version, LV_ALIGN_BOTTOM_MID, 0, -20);

    lv_obj_add_flag(g_welcome_page, LV_OBJ_FLAG_HIDDEN);
}

static void ui_create_weight_page(void)
{
    g_weight_page = lv_obj_create(g_main_screen);
    lv_obj_set_size(g_weight_page, BSP_LCD_H_RES, BSP_LCD_V_RES);
    lv_obj_clear_flag(g_weight_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(g_weight_page, lv_color_hex(0x2a2a2a), 0);
    lv_obj_set_style_border_width(g_weight_page, 0, 0);

    // 标题
    lv_obj_t *title = lv_label_create(g_weight_page);
    lv_label_set_text(title, "重量测量");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 40);

    // 重量显示
    g_weight_label = lv_label_create(g_weight_page);
    lv_label_set_text(g_weight_label, "0.0 kg");
    lv_obj_set_style_text_font(g_weight_label, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(g_weight_label, lv_color_hex(0x00ff00), 0);
    lv_obj_align(g_weight_label, LV_ALIGN_CENTER, 0, -20);

    // 提示信息
    lv_obj_t *tip = lv_label_create(g_weight_page);
    lv_label_set_text(tip, "重量稳定后将自动生成支付二维码");
    lv_obj_set_style_text_font(tip, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(tip, lv_color_hex(0x888888), 0);
    lv_obj_align(tip, LV_ALIGN_CENTER, 0, 40);

    lv_obj_add_flag(g_weight_page, LV_OBJ_FLAG_HIDDEN);
}

static void ui_create_payment_page(void)
{
    g_payment_page = lv_obj_create(g_main_screen);
    lv_obj_set_size(g_payment_page, BSP_LCD_H_RES, BSP_LCD_V_RES);
    lv_obj_clear_flag(g_payment_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(g_payment_page, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_border_width(g_payment_page, 0, 0);

    // 标题
    lv_obj_t *title = lv_label_create(g_payment_page);
    lv_label_set_text(title, "微信支付");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 40);

    // 二维码占位符
    g_qr_code = lv_obj_create(g_payment_page);
    lv_obj_set_size(g_qr_code, 200, 200);
    lv_obj_set_style_bg_color(g_qr_code, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_border_width(g_qr_code, 2, 0);
    lv_obj_set_style_border_color(g_qr_code, lv_color_hex(0x00ff00), 0);
    lv_obj_align(g_qr_code, LV_ALIGN_CENTER, 0, -20);

    // 二维码中心文字
    lv_obj_t *qr_text = lv_label_create(g_qr_code);
    lv_label_set_text(qr_text, "二维码");
    lv_obj_set_style_text_font(qr_text, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(qr_text, lv_color_hex(0x000000), 0);
    lv_obj_center(qr_text);

    // 状态信息
    g_status_label = lv_label_create(g_payment_page);
    lv_label_set_text(g_status_label, "等待支付...");
    lv_obj_set_style_text_font(g_status_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(g_status_label, lv_color_hex(0xffff00), 0);
    lv_obj_align(g_status_label, LV_ALIGN_BOTTOM_MID, 0, -60);

    // 支付提示
    lv_obj_t *tip = lv_label_create(g_payment_page);
    lv_label_set_text(tip, "请使用微信扫描上方二维码完成支付");
    lv_obj_set_style_text_font(tip, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(tip, lv_color_hex(0x888888), 0);
    lv_obj_align(tip, LV_ALIGN_BOTTOM_MID, 0, -30);

    lv_obj_add_flag(g_payment_page, LV_OBJ_FLAG_HIDDEN);
}

static void ui_create_success_page(void)
{
    g_success_page = lv_obj_create(g_main_screen);
    lv_obj_set_size(g_success_page, BSP_LCD_H_RES, BSP_LCD_V_RES);
    lv_obj_clear_flag(g_success_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(g_success_page, lv_color_hex(0x00ff00), 0);
    lv_obj_set_style_border_width(g_success_page, 0, 0);

    // 成功图标 (简单文字代替)
    lv_obj_t *icon = lv_label_create(g_success_page);
    lv_label_set_text(icon, "✓");
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_64, 0);
    lv_obj_set_style_text_color(icon, lv_color_hex(0xffffff), 0);
    lv_obj_align(icon, LV_ALIGN_CENTER, 0, -40);

    // 成功文字
    lv_obj_t *text = lv_label_create(g_success_page);
    lv_label_set_text(text, "支付成功！");
    lv_obj_set_style_text_font(text, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(text, lv_color_hex(0xffffff), 0);
    lv_obj_align(text, LV_ALIGN_CENTER, 0, 20);

    // 感谢信息
    lv_obj_t *thanks = lv_label_create(g_success_page);
    lv_label_set_text(thanks, "感谢使用智能体重秤");
    lv_obj_set_style_text_font(thanks, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(thanks, lv_color_hex(0xffffff), 0);
    lv_obj_align(thanks, LV_ALIGN_BOTTOM_MID, 0, -40);

    lv_obj_add_flag(g_success_page, LV_OBJ_FLAG_HIDDEN);
}

static void ui_show_page(lv_obj_t *page)
{
    lv_obj_add_flag(g_welcome_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_weight_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_payment_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_success_page, LV_OBJ_FLAG_HIDDEN);
    
    if (page) {
        lv_obj_clear_flag(page, LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_acquire(void)
{
    xSemaphoreTake(g_guisemaphore, portMAX_DELAY);
}

void ui_release(void)
{
    xSemaphoreGive(g_guisemaphore);
}

esp_err_t ui_main_start(void)
{
    g_guisemaphore = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(g_guisemaphore, ESP_ERR_NO_MEM, TAG, "Create gui semaphore failed");

    g_main_screen = lv_scr_act();
    
    ui_create_welcome_page();
    ui_create_weight_page();
    ui_create_payment_page();
    ui_create_success_page();
    
    ui_show_page(g_welcome_page);

    ESP_LOGI(TAG, "智能体重秤UI启动完成");
    return ESP_OK;
}

void ui_update_wifi_status(bool connected)
{
    if (!g_wifi_label) return;
    
    ui_acquire();
    lv_label_set_text(g_wifi_label, connected ? "WiFi已连接" : "WiFi连接中...");
    lv_obj_set_style_text_color(g_wifi_label, 
                               connected ? lv_color_hex(0x00ff00) : lv_color_hex(0x888888), 0);
    ui_release();
}

void ui_update_weight(float weight)
{
    if (!g_weight_label) return;
    
    ui_acquire();
    char weight_str[32];
    snprintf(weight_str, sizeof(weight_str), "%.1f kg", weight);
    lv_label_set_text(g_weight_label, weight_str);
    ui_release();
}

void ui_show_weight_page(void)
{
    ui_acquire();
    ui_show_page(g_weight_page);
    ui_release();
}

void ui_show_payment_page(const char *qr_data)
{
    ui_acquire();
    
    if (g_qr_code && qr_data) {
        lv_obj_t *qr_text = lv_obj_get_child(g_qr_code, 0);
        if (qr_text) {
            lv_label_set_text(qr_text, "支付二维码");
        }
    }
    
    if (g_status_label) {
        lv_label_set_text(g_status_label, "等待支付...");
        lv_obj_set_style_text_color(g_status_label, lv_color_hex(0xffff00), 0);
    }
    
    ui_show_page(g_payment_page);
    ui_release();
}

void ui_update_payment_status(wechat_payment_status_t status)
{
    if (!g_status_label) return;
    
    ui_acquire();
    
    const char *status_text;
    lv_color_t status_color;
    
    switch (status) {
        case WECHAT_STATUS_PENDING:
            status_text = "等待支付...";
            status_color = lv_color_hex(0xffff00);
            break;
        case WECHAT_STATUS_PAID:
            status_text = "支付成功！";
            status_color = lv_color_hex(0x00ff00);
            break;
        case WECHAT_STATUS_TIMEOUT:
            status_text = "支付超时";
            status_color = lv_color_hex(0xff0000);
            break;
        case WECHAT_STATUS_FAILED:
            status_text = "支付失败";
            status_color = lv_color_hex(0xff0000);
            break;
        default:
            status_text = "状态未知";
            status_color = lv_color_hex(0x888888);
            break;
    }
    
    lv_label_set_text(g_status_label, status_text);
    lv_obj_set_style_text_color(g_status_label, status_color, 0);
    
    ui_release();
}

void ui_show_success_page(void)
{
    ui_acquire();
    ui_show_page(g_success_page);
    ui_release();
}
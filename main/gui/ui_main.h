/*
 * SPDX-FileCopyrightText: 2015-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "main.h"
#include "lvgl.h"
#include "app_wechat.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    lv_style_t style;
    lv_style_t style_focus_no_outline;
    lv_style_t style_focus;
    lv_style_t style_pr;
} button_style_t;

// UI主函数
esp_err_t ui_main_start(void);
void ui_acquire(void);
void ui_release(void);

// UI更新函数
void ui_update_wifi_status(bool connected);
void ui_update_weight(float weight);
void ui_show_weight_page(void);
void ui_show_payment_page(const char *qr_data);
void ui_update_payment_status(wechat_payment_status_t status);
void ui_show_success_page(void);

// 兼容性函数
lv_group_t *ui_get_btn_op_group(void);
button_style_t *ui_button_styles(void);
lv_obj_t *ui_main_get_status_bar(void);
void ui_main_status_bar_set_wifi(bool is_connected);
void ui_main_status_bar_set_cloud(bool is_connected);

#ifdef __cplusplus
}
#endif
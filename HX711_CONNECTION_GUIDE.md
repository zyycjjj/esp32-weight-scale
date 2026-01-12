# 智能体重秤 - HX711 重量传感器连接指南

## 项目概述

本指南基于ESP-BOX开发板，详细介绍如何集成HX711重量传感器，构建智能体重秤系统。包括硬件接线、软件实现以及扩展功能设计。

## 硬件平台分析

### ESP-BOX硬件特性

ESP-BOX是基于ESP32-S3-WROOM-1模块的AIoT开发平台，具备以下特性：

- **主控芯片**: ESP32-S3-WROOM-1
- **存储**: 16MB Quad Flash + 16MB Octal PSRAM
- **显示**: 2.4英寸 320x240 SPI触摸屏
- **音频**: 双数字麦克风 + 扬声器
- **传感器**: 3轴陀螺仪 + 3轴加速度计
- **扩展接口**: 16个可编程GPIO（通过PCIe连接器）
- **通信**: Wi-Fi + Bluetooth 5 (LE)
- **电源**: Type-C接口

### 可用GPIO资源

ESP-BOX通过高密度PCIe连接器提供16个可编程GPIO，可配置为：
- 数字输入/输出
- I2C、SPI、UART等通信接口
- PWM输出
- ADC输入

## HX711传感器分析

### HX711技术规格

HX711是专为高精度电子秤设计的24位ADC芯片：

- **分辨率**: 24位
- **通道数**: 2个差分输入通道
- **接口**: 串行接口（时钟线+数据线）
- **电源**: 2.6~5.5V
- **可编程增益**: 128或64
- **采样率**: 10/80 SPS

### 引脚定义

基于商家提供的51单片机源码分析：

| 引脚 | 功能 | 描述 |
|------|------|------|
| VCC | 电源 | 正极电源（2.6-5.5V）|
| GND | 地线 | 负极电源 |
| DOUT | 数据输出 | 串行数据输出（接ESP32 GPIO）|
| SCK | 时钟输入 | 串行时钟输入（接ESP32 GPIO）|

## 硬件接线方案

### 已验证接线配置（当前项目）

基于你当前实际接线并已在固件中验证读数正常：

```
HX711    →    ESP-BOX
VCC      →    3V3
GND      →    GND
DOUT/DT  →    G39 (GPIO39)
SCK      →    G40 (GPIO40)
```

对应本地配置文件（不提交到仓库）：

```
.aiw_secrets.env
AIW_HX711_DOUT_PIN=39
AIW_HX711_SCK_PIN=40
```

### 备用接线配置（历史默认）

基于ESP-BOX的GPIO资源分配，建议使用以下接线方案：

```
HX711    →    ESP-BOX
VCC      →    3.3V
GND      →    GND  
DOUT     →    GPIO1
SCK      →    GPIO2
```

### 接线原理图

```
ESP-BOX          HX711模块
-------        ------------
3.3V    ─────→  VCC
GND     ─────→  GND
GPIO1   ─────→  DOUT
GPIO2   ─────→  SCK
```

### 接线注意事项

1. **电源连接**:
   - 使用ESP-BOX的3.3V电源输出
   - 确保电源稳定，建议添加10μF去耦电容

2. **信号线连接**:
   - GPIO1配置为输入模式，读取DOUT信号
   - GPIO2配置为输出模式，提供SCK时钟信号

3. **抗干扰措施**:
   - 信号线尽量短，避免长距离传输
   - 远离高频信号源
   - 使用屏蔽线或双绞线

## 软件实现

### HX711驱动代码（ESP32适配）

基于商家提供的51单片机代码，适配ESP32的实现：

```c
#include <driver/gpio.h>
#include "esp_log.h"

#define HX711_DOUT_PIN    GPIO_NUM_1
#define HX711_SCK_PIN     GPIO_NUM_2

static const char *TAG = "HX711";

// 初始化GPIO
void hx711_init(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << HX711_DOUT_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
    
    io_conf.pin_bit_mask = (1ULL << HX711_SCK_PIN);
    io_conf.mode = GPIO_MODE_OUTPUT;
    gpio_config(&io_conf);
    
    // 初始状态：SCK为低电平
    gpio_set_level(HX711_SCK_PIN, 0);
    ESP_LOGI(TAG, "HX711 initialized");
}

// 微秒级延时
void hx711_delay_us(uint32_t us) {
    ets_delay_us(us);
}

// 读取HX711数据
int32_t hx711_read(void) {
    int32_t count = 0;
    
    // 等待DOUT变为低电平
    while(gpio_get_level(HX711_DOUT_PIN)) {
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    
    // 读取24位数据
    for(int i = 0; i < 24; i++) {
        gpio_set_level(HX711_SCK_PIN, 1);
        hx711_delay_us(1);
        count = count << 1;
        gpio_set_level(HX711_SCK_PIN, 0);
        hx711_delay_us(1);
        
        if(gpio_get_level(HX711_DOUT_PIN)) {
            count++;
        }
    }
    
    // 第25个时钟脉冲设置增益和通道
    gpio_set_level(HX711_SCK_PIN, 1);
    hx711_delay_us(1);
    gpio_set_level(HX711_SCK_PIN, 0);
    hx711_delay_us(1);
    
    // 转换为有符号整数
    if(count & 0x800000) {
        count |= 0xFF000000;
    }
    
    return count;
}

// 获取重量值（需要校准）
float get_weight(int32_t raw_value, float scale_factor) {
    return (float)(raw_value - OFFSET) / scale_factor;
}
```

### 校准程序

```c
// 校准参数
#define OFFSET      8388608  // 零点偏移
#define SCALE_FACTOR 1000.0f  // 比例系数（需要实际校准）

// 校准函数
void calibrate_hx711(void) {
    ESP_LOGI(TAG, "开始校准，请确保秤盘上无物品");
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    int32_t zero_offset = 0;
    for(int i = 0; i < 10; i++) {
        zero_offset += hx711_read();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    zero_offset /= 10;
    
    ESP_LOGI(TAG, "零点偏移: %ld", zero_offset);
    
    ESP_LOGI(TAG, "请放置已知重量的物品（如1kg砝码）");
    vTaskDelay(pdMS_TO_TICKS(5000));
    
    int32_t weight_value = 0;
    for(int i = 0; i < 10; i++) {
        weight_value += hx711_read();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    weight_value /= 10;
    
    float scale = (float)(weight_value - zero_offset) / 1000.0f;
    ESP_LOGI(TAG, "比例系数: %.2f", scale);
}
```

## 扩展功能设计

### 1. 触摸屏和按钮身高输入

#### 用户界面设计

利用ESP-BOX的2.4英寸触摸屏，设计友好的用户界面：

```c
// LVGL界面示例
void create_height_input_screen(void) {
    // 创建身高输入屏幕
    lv_obj_t *screen = lv_obj_create(NULL);
    
    // 标题
    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "身高输入");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);
    
    // 身高显示
    lv_obj_t *height_label = lv_label_create(screen);
    lv_label_set_text(height_label, "170 cm");
    lv_obj_set_style_text_font(height_label, &lv_font_montserrat_32, 0);
    lv_obj_align(height_label, LV_ALIGN_CENTER, 0, 0);
    
    // 增减按钮
    lv_obj_t *btn_inc = lv_btn_create(screen);
    lv_obj_set_size(btn_inc, 80, 50);
    lv_obj_align(btn_inc, LV_ALIGN_CENTER, -100, 100);
    lv_obj_t *label_inc = lv_label_create(btn_inc);
    lv_label_set_text(label_inc, "+");
    lv_obj_center(label_inc);
    
    lv_obj_t *btn_dec = lv_btn_create(screen);
    lv_obj_set_size(btn_dec, 80, 50);
    lv_obj_align(btn_dec, LV_ALIGN_CENTER, 100, 100);
    lv_obj_t *label_dec = lv_label_create(btn_dec);
    lv_label_set_text(label_dec, "-");
    lv_obj_center(label_dec);
    
    // 确认按钮
    lv_obj_t *btn_confirm = lv_btn_create(screen);
    lv_obj_set_size(btn_confirm, 120, 50);
    lv_obj_align(btn_confirm, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_t *label_confirm = lv_label_create(btn_confirm);
    lv_label_set_text(label_confirm, "确认");
    lv_obj_center(label_confirm);
    
    lv_scr_load(screen);
}
```

#### 按钮事件处理

```c
// 身高变量
static int user_height = 170;

// 按钮回调函数
void btn_event_cb(lv_event_t *e) {
    lv_obj_t *btn = lv_event_get_target(e);
    lv_obj_t *height_label = lv_obj_get_user_data(btn);
    
    if(btn == btn_inc) {
        if(user_height < 250) {
            user_height++;
        }
    } else if(btn == btn_dec) {
        if(user_height > 50) {
            user_height--;
        }
    } else if(btn == btn_confirm) {
        // 保存身高并切换到称重界面
        save_user_height(user_height);
        switch_to_weight_screen();
        return;
    }
    
    // 更新显示
    char height_str[20];
    snprintf(height_str, sizeof(height_str), "%d cm", user_height);
    lv_label_set_text(height_label, height_str);
}
```

### 2. 预留扩展接口

#### 打印机接口设计

为热敏打印机预留GPIO接口：

```
打印机      →    ESP-BOX
VCC        →    5V（外接电源）
GND        →    GND
TX         →    GPIO3
RX         →    GPIO4
```

```c
// 打印机接口定义
#define PRINTER_TX_PIN    GPIO_NUM_3
#define PRINTER_RX_PIN    GPIO_NUM_4

// 初始化打印机串口
void init_printer_uart(void) {
    uart_config_t uart_config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    
    uart_driver_install(UART_NUM_1, 1024, 1024, 0, NULL, 0);
    uart_param_config(UART_NUM_1, &uart_config);
    uart_set_pin(UART_NUM_1, PRINTER_TX_PIN, PRINTER_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

// 打印称重结果
void print_weight_result(float weight, int height, float bmi) {
    char print_buffer[256];
    snprintf(print_buffer, sizeof(print_buffer), 
             "=== 智能体重称 ===\n"
             "体重: %.1f kg\n"
             "身高: %d cm\n"
             "BMI: %.1f\n"
             "时间: %s\n"
             "================\n\n",
             weight, height, bmi, get_current_time());
    
    uart_write_bytes(UART_NUM_1, print_buffer, strlen(print_buffer));
}
```

#### 扭蛋机接口设计

为扭蛋机预留控制接口：

```
扭蛋机      →    ESP-BOX
VCC        →    5V（外接电源）
GND        →    GND
Motor      →    GPIO5（通过MOSFET驱动）
Sensor     →    GPIO6（位置检测）
```

```c
// 扭蛋机控制引脚
#define GASHAPON_MOTOR_PIN   GPIO_NUM_5
#define GASHAPON_SENSOR_PIN  GPIO_NUM_6

// 初始化扭蛋机控制
void init_gashapon_control(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GASHAPON_MOTOR_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
    
    io_conf.pin_bit_mask = (1ULL << GASHAPON_SENSOR_PIN);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io_conf);
    
    gpio_set_level(GASHAPON_MOTOR_PIN, 0);
}

// 启动扭蛋机
void start_gashapon_machine(void) {
    ESP_LOGI(TAG, "启动扭蛋机");
    gpio_set_level(GASHAPON_MOTOR_PIN, 1);
    
    // 等待传感器检测到位
    int timeout = 0;
    while(gpio_get_level(GASHAPON_SENSOR_PIN) && timeout < 100) {
        vTaskDelay(pdMS_TO_TICKS(100));
        timeout++;
    }
    
    // 停止电机
    gpio_set_level(GASHAPON_MOTOR_PIN, 0);
    
    if(timeout < 100) {
        ESP_LOGI(TAG, "扭蛋机运行完成");
        return 1;
    } else {
        ESP_LOGW(TAG, "扭蛋机运行超时");
        return 0;
    }
}
```

## 完整系统集成

### 主程序流程

```c
void app_main(void) {
    // 初始化硬件
    hx711_init();
    init_printer_uart();
    init_gashapon_control();
    init_lvgl_display();
    
    // 创建主任务
    xTaskCreate(weight_scale_task, "weight_scale", 4096, NULL, 5, NULL);
}

void weight_scale_task(void *arg) {
    // 显示身高输入界面
    create_height_input_screen();
    
    // 等待用户输入身高
    while(!height_confirmed) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    // 切换到称重界面
    create_weight_screen();
    
    // 开始称重
    float weight = measure_stable_weight();
    float bmi = calculate_bmi(weight, user_height);
    
    // 显示结果
    display_result(weight, user_height, bmi);
    
    // 打印结果（可选）
    if(printer_enabled) {
        print_weight_result(weight, user_height, bmi);
    }
    
    // 启动扭蛋机（奖励）
    if(gashapon_enabled && weight > 0) {
        start_gashapon_machine();
    }
    
    ESP_LOGI(TAG, "测量完成 - 体重: %.1fkg, 身高: %dcm, BMI: %.1f", 
             weight, user_height, bmi);
}
```

### 系统配置文件

```c
// config.h
#ifndef CONFIG_H
#define CONFIG_H

// HX711配置
#define HX711_DOUT_PIN        GPIO_NUM_1
#define HX711_SCK_PIN         GPIO_NUM_2

// 打印机配置
#define PRINTER_UART_NUM      UART_NUM_1
#define PRINTER_TX_PIN        GPIO_NUM_3
#define PRINTER_RX_PIN        GPIO_NUM_4
#define PRINTER_BAUD_RATE     9600

// 扭蛋机配置
#define GASHAPON_MOTOR_PIN    GPIO_NUM_5
#define GASHAPON_SENSOR_PIN   GPIO_NUM_6
#define GASHAPON_TIMEOUT_MS   10000

// 系统配置
#define WEIGHT_STABLE_SAMPLES 10
#define WEIGHT_STABLE_THRESHOLD 0.1f
#define HEIGHT_MIN            50
#define HEIGHT_MAX            250

#endif
```

## 开发环境配置

### ESP-IDF环境搭建

1. **安装依赖**：
   ```bash
   brew install cmake ninja dfu-util
   ```

2. **获取ESP-IDF**：
   ```bash
   git clone --recursive https://github.com/espressif/esp-idf.git
   cd esp-idf
   ./install.sh esp32
   ```

3. **设置环境**：
   ```bash
   source export.sh
   ```

### 项目构建

1. **创建项目**：
   ```bash
   idf.py create-project esp32-weight-scale
   cd esp32-weight-scale
   ```

2. **配置项目**：
   ```bash
   idf.py menuconfig
   ```

3. **编译烧录**：
   ```bash
   idf.py build flash monitor
   ```

## 测试与校准

### 硬件测试

1. **连接测试**：
   - 检查电源连接
   - 验证信号线连接
   - 测试GPIO功能

2. **通信测试**：
   - 读取HX711原始数据
   - 验证数据稳定性
   - 检查噪声水平

### 系统校准

1. **零点校准**：
   - 空载时读取基准值
   - 设置零点偏移

2. **重量校准**：
   - 使用标准砝码
   - 计算比例系数
   - 验证测量精度

## 故障排除

### 常见问题

1. **读数不稳定**：
   - 检查电源稳定性
   - 确认接线牢固
   - 添加滤波电容

2. **通信失败**：
   - 验证GPIO配置
   - 检查时序要求
   - 确认引脚连接

3. **显示异常**：
   - 检查LVGL初始化
   - 验证触摸校准
   - 确认内存配置

## 性能优化

### 软件优化

1. **采样优化**：
   - 多次采样平均
   - 数字滤波算法
   - 异常值剔除

2. **功耗优化**：
   - 深度睡眠模式
   - 按键唤醒机制
   - 动态频率调整

### 硬件优化

1. **抗干扰**：
   - 屏蔽线缆
   - 滤波电路
   - 接地优化

2. **稳定性**：
   - 温度补偿
   - 长期漂移校正
   - 定期自动校准

## 总结

本指南提供了基于ESP-BOX的智能体重秤完整解决方案，包括：

- **硬件接线**：HX711与ESP-BOX的详细连接方案
- **软件实现**：完整的驱动代码和应用逻辑
- **扩展功能**：身高输入、打印机、扭蛋机接口
- **系统集成**：完整的项目构建和部署流程

通过本指南，开发者可以快速构建功能完整的智能体重秤系统，并根据实际需求进行定制和扩展。

## HX711 重量传感器接线图

### 引脚说明
```
HX711 重量传感器模块    ESP32 开发板
VCC                 →  3.3V (或 5V，根据传感器规格)
GND                 →  GND
DOUT (数据输出)     →  GPIO4  (ESP32S3)
SCK (时钟输入)      →  GPIO5  (ESP32S3)
```

### 接线步骤

#### 1. 硬件连接
```
1. 将 HX711 模块的 VCC 连接到 ESP32 的 3.3V 电源
2. 将 HX711 模块的 GND 连接到 ESP32 的 GND
3. 将 HX711 模块的 DOUT 连接到 ESP32 的 GPIO4
4. 将 HX711 模块的 SCK 连接到 ESP32 的 GPIO5
```

#### 2. 重量传感器连接
HX711 模块通常有4个接线端子连接到称重传感器：
```
RED   → E+ (激励正极)
BLACK → E- (激励负极)
WHITE → A- (信号负极)
GREEN → A+ (信号正极)
```

#### 3. 接线注意事项
- 确保电源电压稳定（推荐 3.3V）
- 连接线要牢固，避免接触不良
- 传感器线缆尽量远离电源线，避免干扰
- 如使用 5V 供电，确保 HX711 模块支持 5V 输入

## 配置参数

### 软件配置
项目中已预配置 HX711 参数：
```c
#define HX711_DOUT_PIN     GPIO_NUM_4    // 数据引脚
#define HX711_SCK_PIN      GPIO_NUM_5    // 时钟引脚

// 重量秤配置
.scale_config = {
    .stable_threshold = 0.1f,        // 稳定阈值: 0.1kg
    .stable_samples = 5,              // 连续5次稳定读数
    .payment_timeout_seconds = 300,   // 支付超时: 5分钟
    .minimum_weight = 5.0f,           // 最小测量重量: 5kg
    .maximum_weight = 200.0f          // 最大测量重量: 200kg
}
```

### 校准步骤
1. **零点校准**：空载时读取基准值
2. **砝码校准**：放置已知重量砝码，计算比例系数
3. **验证测试**：测试不同重量点，确保准确性

## 故障排除

### 常见问题
1. **读数不稳定**
   - 检查接线是否牢固
   - 确保电源电压稳定
   - 检查传感器安装是否牢固

2. **读数异常**
   - 检查引脚连接是否正确
   - 验证传感器是否损坏
   - 重新进行校准

3. **通信失败**
   - 确认 GPIO 引脚配置
   - 检查电源供应
   - 验证 HX711 模块工作状态

## 系统集成

HX711 模块已集成到智能体重秤系统中：
- 自动重量检测
- 稳定性判断
- 与支付系统联动
- 实时数据显示

完成硬件连接后，系统将自动：
1. 初始化 HX711 传感器
2. 检测重量变化
3. 判断重量稳定性
4. 触发支付流程
5. 显示测量结果

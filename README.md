# ESP-BOX Weight Scale (Arduino + PlatformIO)

最小可用固件：ESP-BOX 屏幕稳定点亮（ST7789：N C0），并可进行 HX711 读数验证。

## 快速开始

### 硬件

- ESP32-S3-BOX / ESP32-S3-BOX-3
- HX711 模块（可选，用于称重）
- 负载传感器（可选）

默认 HX711 引脚：

- DOUT: GPIO1
- SCK: GPIO2

### 一键编译烧录

```bash
make flash_monitor
```

常用命令：

- `make flash`：只烧录
- `make monitor`：只打开串口
- `make port_check` / `make port_free`：检查/释放串口占用

## 目录结构

- `src/main.cpp`：Arduino 固件入口（屏幕 + HX711）
- `platformio.ini`：PlatformIO 配置
- `Makefile`：一键命令封装
- `HX711_CONNECTION_GUIDE.md`：HX711 接线说明
- `../TOUCH_CALIBRATION.md`：GT911 触摸校准与量产固化说明

## 当前完成情况（触摸）

- GT911 触摸读取已适配两种 8 字节数据布局，并加入布局锁定避免触摸乱跳
- 四角仿射校准已稳定可用（串口 `a`），校准后立即启用映射（无需重编译即可验证）
- 校准结果可直接固化到固件：校准后会打印可复制的 `#define AIW_TOUCH_...` 宏
- 量产方式已切换为“代码/编译宏固化”，不依赖硬件 NVS 持久化
- 已移除触摸时在屏幕绘制的小黑点

### 本批次已固化参数

固化位置：`platformio.ini -> build_flags`

- `AIW_TOUCH_MAP_MODE=1`
- `AIW_TOUCH_AFFINE_ENABLED=1`
- `AIW_TOUCH_AFFINE_A=0.028266951f`
- `AIW_TOUCH_AFFINE_B=0.996659875f`
- `AIW_TOUCH_AFFINE_C=0.001093713f`
- `AIW_TOUCH_AFFINE_D=1.003177404f`
- `AIW_TOUCH_AFFINE_E=-0.012897088f`
- `AIW_TOUCH_AFFINE_F=-0.008323768f`

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

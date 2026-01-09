# ESP32 AI体重秤项目

## 项目简介
基于ESP32-S3-BOX-3开发板的AI体重秤系统，集成重量测量、身高输入、支付、AI评论生成、语音播放、打印和扭蛋机控制功能。

## 硬件配置
- 开发板：ESP32-S3-BOX-3
- 重量传感器：HX711 + 压力传感器
- 打印机：DP-EH700热敏打印机
- 显示：板载LCD屏幕
- 输入：BOOT按键
- 输出：板载扬声器、扭蛋机控制

## 引脚定义
```cpp
#define HX711_DT 4     // HX711数据线
#define HX711_SCK 5    // HX711时钟线
#define GACHA_PIN 2    // 扭蛋机控制信号线
#define PRINTER_TX 17  // 打印机发送
#define PRINTER_RX 16  // 打印机接收
#define BOOT_BUTTON 0  // BOOT按键
```

## 系统状态机
1. **INIT** - 系统初始化
2. **WEIGHT_INPUT** - 体重测量
3. **HEIGHT_INPUT** - 身高输入
4. **PAYMENT_QR** - 显示支付二维码
5. **PAYMENT_WAIT** - 等待支付完成
6. **AI_GENERATING** - 生成AI文案
7. **VOICE_PLAYING** - 语音播放中
8. **PRINTING** - 打印小票
9. **GACHA_DROP** - 扭蛋掉落
10. **COMPLETE** - 流程完成

## 开发环境设置

### 1. 安装PlatformIO
```bash
# 安装VSCode扩展：PlatformIO IDE
# 或者安装CLI版本
pip install platformio
```

### 2. 项目结构
```
esp32-weight-scale/
├── platformio.ini      # PlatformIO配置文件
├── src/
│   ├── main.cpp        # 主程序
│   ├── wifi_manager.h  # WiFi管理（待实现）
│   ├── payment.h       # 支付相关（待实现）
│   ├── tts.h          # 语音播放（待实现）
│   └── printer.h      # 打印机控制（待实现）
└── lib/               # 自定义库
```

### 3. 编译和上传
```bash
# 编译项目
pio run

# 上传到开发板
pio run --target upload

# 监控串口输出
pio device monitor
```

## 功能说明

### 重量测量
- 使用HX711读取压力传感器数据
- 支持多种量程传感器（10/20/40/100/200Kg）
- 自动校准和滤波

### 身高输入
- 使用BOOT按键进行身高输入
- 长按增加身高值
- 短按确认输入

### 支付功能
- 生成唯一订单ID
- 显示支付二维码
- 等待支付结果

### AI评论
- 调用后端API获取个性化AI评论
- 基于BMI计算结果生成不同类型评论

### 语音播放
- 集成文字转语音功能
- 播放AI生成的评论

### 打印功能
- 使用ESC/POS协议控制热敏打印机
- 打印体重数据和AI评论

### 扭蛋机控制
- 通过GPIO控制扭蛋机
- 低电平脉冲触发扭蛋掉落

## 配置参数

### WiFi配置
```cpp
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
```

### 服务器配置
```cpp
const char* serverUrl = "http://ai.youtirj.com/api";
```

### HX711校准
```cpp
float calibrationFactor = -7050; // 需要根据实际传感器校准
```

## 待完成功能
- [ ] WiFi连接优化
- [ ] 二维码生成和显示
- [ ] 支付状态检查
- [ ] TTS语音播放
- [ ] 打印机驱动优化
- [ ] 系统错误处理
- [ ] 数据存储功能
- [ ] OTA升级支持

## 调试说明
- 使用串口监视器查看调试信息
- 波特率：115200
- 可通过TFT屏幕查看系统状态

## 注意事项
1. 首次使用需要校准HX711传感器
2. 确保WiFi连接正常
3. 检查服务器API地址配置
4. 注意GPIO引脚分配
5. 定期检查硬件连接
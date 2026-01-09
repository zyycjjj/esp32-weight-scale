#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <HX711.h>
#include <TFT_eSPI.h>
#include <QRCode.h>
#include <SoftwareSerial.h>

// 硬件引脚定义
#define HX711_DT 4     // HX711数据线
#define HX711_SCK 5    // HX711时钟线
#define GACHA_PIN 2    // 扭蛋机控制信号线
#define PRINTER_TX 17  // 打印机发送
#define PRINTER_RX 16  // 打印机接收
#define BOOT_BUTTON 0  // BOOT按键

// WiFi配置
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
const char* serverUrl = "http://ai.youtirj.com/api";

// 系统状态枚举
enum SystemState {
  INIT,           // 系统初始化
  WEIGHT_INPUT,   // 体重测量
  HEIGHT_INPUT,   // 身高输入
  PAYMENT_QR,     // 显示支付二维码
  PAYMENT_WAIT,   // 等待支付完成
  AI_GENERATING,  // 生成AI文案
  VOICE_PLAYING,  // 语音播放中
  PRINTING,       // 打印中
  GACHA_DROP,     // 扭蛋掉落
  COMPLETE        // 流程完成
};

// 全局变量
SystemState currentState = INIT;
HX711 scale;
TFT_eSPI tft = TFT_eSPI();
QRCode qrcode;
SoftwareSerial printerSerial(PRINTER_RX, PRINTER_TX);

// 用户数据
float userWeight = 0;
float userHeight = 0;
String orderId = "";
String aiComment = "";
int heightCounter = 150; // 默认身高

// HX711校准参数
float calibrationFactor = -7050; // 需要根据实际传感器校准

void setup() {
  Serial.begin(115200);
  
  // 初始化硬件
  initHardware();
  
  // 连接WiFi
  connectWiFi();
  
  // 显示欢迎界面
  showWelcomeScreen();
  
  currentState = WEIGHT_INPUT;
}

void loop() {
  switch (currentState) {
    case INIT:
      handleInit();
      break;
    case WEIGHT_INPUT:
      handleWeightInput();
      break;
    case HEIGHT_INPUT:
      handleHeightInput();
      break;
    case PAYMENT_QR:
      handlePaymentQR();
      break;
    case PAYMENT_WAIT:
      handlePaymentWait();
      break;
    case AI_GENERATING:
      handleAIGenerating();
      break;
    case VOICE_PLAYING:
      handleVoicePlaying();
      break;
    case PRINTING:
      handlePrinting();
      break;
    case GACHA_DROP:
      handleGachaDrop();
      break;
    case COMPLETE:
      handleComplete();
      break;
  }
  
  delay(100);
}

void initHardware() {
  // 初始化HX711
  scale.begin(HX711_DT, HX711_SCK);
  scale.set_scale(calibrationFactor);
  scale.tare();
  
  // 初始化TFT屏幕
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  
  // 初始化按键
  pinMode(BOOT_BUTTON, INPUT_PULLUP);
  
  // 初始化扭蛋机控制
  pinMode(GACHA_PIN, OUTPUT);
  digitalWrite(GACHA_PIN, HIGH);
  
  // 初始化打印机串口
  printerSerial.begin(9600);
  
  Serial.println("硬件初始化完成");
}

void connectWiFi() {
  WiFi.begin(ssid, password);
  
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.drawString("连接WiFi...", 10, 50);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("WiFi连接成功");
  tft.drawString("WiFi连接成功", 10, 80);
  delay(1000);
}

void showWelcomeScreen() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(3);
  tft.drawString("AI体重秤", 50, 50);
  tft.setTextSize(2);
  tft.drawString("请站上体重秤", 50, 100);
}

void handleInit() {
  // 初始化状态处理
  currentState = WEIGHT_INPUT;
}

void handleWeightInput() {
  // 读取重量数据
  if (scale.is_ready()) {
    float weight = abs(scale.get_units());
    
    if (weight > 5 && weight < 200) { // 有效重量范围
      userWeight = weight;
      tft.fillScreen(TFT_BLACK);
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.setTextSize(2);
      tft.drawString("体重: " + String(weight, 1) + "kg", 50, 80);
      tft.drawString("按BOOT键输入身高", 50, 120);
      
      // 等待用户输入身高
      if (digitalRead(BOOT_BUTTON) == LOW) {
        delay(50); // 防抖
        if (digitalRead(BOOT_BUTTON) == LOW) {
          currentState = HEIGHT_INPUT;
        }
      }
    } else {
      showWelcomeScreen();
    }
  }
  
  delay(100);
}

void handleHeightInput() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.drawString("身高: " + String(heightCounter) + "cm", 50, 80);
  tft.drawString("长按增加", 50, 120);
  tft.drawString("短按确认", 50, 150);
  
  static unsigned long pressStartTime = 0;
  static bool isPressed = false;
  
  if (digitalRead(BOOT_BUTTON) == LOW) {
    if (!isPressed) {
      pressStartTime = millis();
      isPressed = true;
    } else {
      if (millis() - pressStartTime > 500) {
        // 长按增加身高
        heightCounter++;
        if (heightCounter > 220) heightCounter = 100;
        delay(200);
      }
    }
  } else {
    if (isPressed) {
      if (millis() - pressStartTime < 500) {
        // 短按确认身高
        userHeight = heightCounter;
        currentState = PAYMENT_QR;
      }
      isPressed = false;
    }
  }
  
  delay(50);
}

void handlePaymentQR() {
  // 生成订单ID
  orderId = "ORDER_" + String(millis());
  
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.drawString("请扫码支付", 50, 50);
  
  // 这里应该显示二维码，简化处理
  tft.drawString("订单号:", 50, 100);
  tft.drawString(orderId, 50, 130);
  tft.drawString("金额: 2元", 50, 160);
  
  // 模拟支付过程
  delay(5000);
  currentState = PAYMENT_WAIT;
}

void handlePaymentWait() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.drawString("等待支付...", 50, 80);
  
  // 模拟支付成功
  delay(3000);
  currentState = AI_GENERATING;
}

void handleAIGenerating() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.drawString("生成AI评论中...", 50, 80);
  
  // 调用后端API获取AI评论
  HTTPClient http;
  http.begin(serverUrl + "/get_ai_comment");
  http.addHeader("Content-Type", "application/json");
  
  String jsonPayload = "{\"weight\":" + String(userWeight) + ",\"height\":" + String(userHeight) + "}";
  
  int httpResponseCode = http.POST(jsonPayload);
  
  if (httpResponseCode > 0) {
    String response = http.getString();
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, response);
    
    if (doc["success"]) {
      aiComment = doc["data"]["comment"].as<String>();
      currentState = VOICE_PLAYING;
    } else {
      aiComment = "你的体重数据很有意思！";
      currentState = VOICE_PLAYING;
    }
  } else {
    aiComment = "无法获取AI评论，请稍后再试！";
    currentState = VOICE_PLAYING;
  }
  
  http.end();
}

void handleVoicePlaying() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.drawString("播放语音:", 50, 50);
  tft.setTextSize(1);
  tft.drawString(aiComment, 10, 100);
  
  // 模拟语音播放
  delay(3000);
  currentState = PRINTING;
}

void handlePrinting() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.drawString("打印中...", 50, 80);
  
  // 打印小票
  printReceipt();
  
  delay(2000);
  currentState = GACHA_DROP;
}

void printReceipt() {
  // ESC/POS打印命令
  printerSerial.write(0x1B); // ESC
  printerSerial.write(0x40); // @
  
  printerSerial.println("=== AI体重秤 ===");
  printerSerial.println("时间: " + String(millis()));
  printerSerial.println("体重: " + String(userWeight, 1) + "kg");
  printerSerial.println("身高: " + String(userHeight) + "cm");
  printerSerial.println("BMI: " + String(userWeight / ((userHeight/100) * (userHeight/100)), 1));
  printerSerial.println("AI评论:");
  printerSerial.println(aiComment);
  printerSerial.println("================");
  
  // 切纸命令
  printerSerial.write(0x1D);
  printerSerial.write(0x56);
  printerSerial.write(0x00);
}

void handleGachaDrop() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.drawString("掉落扭蛋中...", 50, 80);
  
  // 触发扭蛋机
  digitalWrite(GACHA_PIN, LOW);
  delay(200);
  digitalWrite(GACHA_PIN, HIGH);
  
  delay(2000);
  currentState = COMPLETE;
}

void handleComplete() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(3);
  tft.drawString("完成!", 80, 80);
  tft.setTextSize(2);
  tft.drawString("感谢使用", 60, 120);
  
  delay(5000);
  
  // 重置系统
  currentState = WEIGHT_INPUT;
  userWeight = 0;
  userHeight = 0;
  orderId = "";
  aiComment = "";
  heightCounter = 150;
}
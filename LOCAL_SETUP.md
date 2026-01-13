# 本地联调说明（esp32-weight-scale + ai-weight-backend）

## 1) 后端（ai-weight-backend）

```bash
cd ../ai-weight-backend
pnpm i
pnpm start:dev
```

默认端口 3000。

### 本地 mock 支付

在 `ai-weight-backend/.env` 中设置：

```
AIW_MOCK_PAY=1
```

## 2) 固件（esp32-weight-scale）

### 2.1 配置

复制并编辑：

```bash
cp .aiw_secrets.env.example .aiw_secrets.env
```

关键项：
- `AIW_WIFI_SSID / AIW_WIFI_PASSWORD`
- `AIW_BACKEND_BASE_URL`
  - 本地联调：`http://<你的电脑局域网IP>:3000`
  - 线上服务：`https://ai.youtirj.com`

### 2.2 编译与烧录

```bash
make flash_monitor
```

### 2.3 验证

- 身高选择：触摸左右滑动或点左右键调整，点 NEXT 确认进入称重（BOOT 仅作备用）
- 称重稳定后延迟约 0.9s 自动出二维码（避免误触发）
- 称重页支持触摸按钮：TARE 去皮，BACK 返回身高选择
- 支付页支持触摸按钮：CANCEL 取消并返回称重
- 串口输入 `q` 可强制触发下单
- 串口输入 `9` 可直接走线上 TTS 合成并播放（便于联调）
- 支付成功后拉取 `/api/get_ai_comment_with_tts`，同步打印与播报

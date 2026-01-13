#include "app/audio_player.h"

#include "app/gacha_controller.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <driver/i2s.h>

namespace aiw {

static String joinUrl(const char *baseUrl, const String &path) {
  if (!baseUrl || !baseUrl[0]) return path;
  String base(baseUrl);
  if (path.startsWith("http://") || path.startsWith("https://")) return path;
  if (!path.length()) return base;
  if (base.endsWith("/") && path.startsWith("/")) return base + path.substring(1);
  if (!base.endsWith("/") && !path.startsWith("/")) return base + "/" + path;
  return base + path;
}

void AudioPlayer::begin(bool enabled, int bclkPin, int lrckPin, int doutPin, int volume) {
  enabled_ = enabled && bclkPin >= 0 && lrckPin >= 0 && doutPin >= 0;
  bclkPin_ = bclkPin;
  lrckPin_ = lrckPin;
  doutPin_ = doutPin;
  volume_ = volume;
  playing_ = false;
}

static uint16_t readU16LE(const uint8_t *p) {
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t readU32LE(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool isHttpsUrl(const String &url) {
  return url.startsWith("https://");
}

static bool i2sInit(int sampleRate, int bclkPin, int lrckPin, int doutPin) {
  i2s_driver_uninstall(I2S_NUM_0);

  i2s_config_t i2sConfig{};
  i2sConfig.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  i2sConfig.sample_rate = sampleRate;
  i2sConfig.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  i2sConfig.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  i2sConfig.communication_format = I2S_COMM_FORMAT_I2S_MSB;
  i2sConfig.intr_alloc_flags = 0;
  i2sConfig.dma_buf_count = 8;
  i2sConfig.dma_buf_len = 256;
  i2sConfig.use_apll = false;
  i2sConfig.tx_desc_auto_clear = true;
  i2sConfig.fixed_mclk = 0;

  if (i2s_driver_install(I2S_NUM_0, &i2sConfig, 0, nullptr) != ESP_OK) {
    return false;
  }

  i2s_pin_config_t pins{};
  pins.bck_io_num = bclkPin;
  pins.ws_io_num = lrckPin;
  pins.data_out_num = doutPin;
  pins.data_in_num = I2S_PIN_NO_CHANGE;
  if (i2s_set_pin(I2S_NUM_0, &pins) != ESP_OK) {
    i2s_driver_uninstall(I2S_NUM_0);
    return false;
  }
  i2s_zero_dma_buffer(I2S_NUM_0);
  return true;
}

static int16_t scaleSample(int16_t s, int volume) {
  if (volume <= 0) return 0;
  if (volume >= 21) return s;
  int32_t v = (int32_t)s * (int32_t)volume;
  v /= 21;
  if (v > 32767) v = 32767;
  if (v < -32768) v = -32768;
  return (int16_t)v;
}

bool AudioPlayer::playWav(const char *baseUrl, const String &audioUrlOrPath, GachaController *gacha) {
  if (!enabled_) return false;
  String url = joinUrl(baseUrl, audioUrlOrPath);
  if (!url.length()) return false;

  int httpCode = -1;
  HTTPClient http;
  WiFiClientSecure secureClient;
  WiFiClient plainClient;

  if (isHttpsUrl(url)) {
    secureClient.setInsecure();
    if (!http.begin(secureClient, url)) return false;
  } else {
    if (!http.begin(plainClient, url)) return false;
  }

  httpCode = http.GET();
  if (httpCode < 200 || httpCode >= 300) {
    http.end();
    return false;
  }

  WiFiClient *stream = http.getStreamPtr();

  uint8_t riff[12];
  if (stream->readBytes(riff, sizeof(riff)) != sizeof(riff)) {
    http.end();
    return false;
  }
  if (memcmp(riff, "RIFF", 4) != 0 || memcmp(riff + 8, "WAVE", 4) != 0) {
    http.end();
    return false;
  }

  uint16_t audioFormat = 0;
  uint16_t numChannels = 0;
  uint32_t sampleRate = 0;
  uint16_t bitsPerSample = 0;
  uint32_t dataSize = 0;

  while (stream->connected()) {
    uint8_t hdr[8];
    if (stream->readBytes(hdr, sizeof(hdr)) != sizeof(hdr)) break;
    uint32_t chunkSize = readU32LE(hdr + 4);
    if (memcmp(hdr, "fmt ", 4) == 0) {
      uint8_t fmt[32];
      uint32_t toRead = chunkSize < sizeof(fmt) ? chunkSize : sizeof(fmt);
      if (stream->readBytes(fmt, toRead) != toRead) break;
      uint32_t remain = chunkSize - toRead;
      while (remain > 0) {
        uint8_t tmp[64];
        uint32_t n = remain > sizeof(tmp) ? sizeof(tmp) : remain;
        size_t got = stream->readBytes(tmp, n);
        if (got == 0) {
          remain = 0;
          break;
        }
        remain -= (uint32_t)got;
      }
      audioFormat = readU16LE(fmt + 0);
      numChannels = readU16LE(fmt + 2);
      sampleRate = readU32LE(fmt + 4);
      bitsPerSample = readU16LE(fmt + 14);
    } else if (memcmp(hdr, "data", 4) == 0) {
      dataSize = chunkSize;
      break;
    } else {
      while (chunkSize > 0) {
        uint8_t buf[64];
        uint32_t n = chunkSize > sizeof(buf) ? sizeof(buf) : chunkSize;
        if (stream->readBytes(buf, n) != n) {
          chunkSize = 0;
          break;
        }
        chunkSize -= n;
      }
    }
  }

  if (audioFormat != 1 || (numChannels != 1 && numChannels != 2) || bitsPerSample != 16 || sampleRate == 0 || dataSize == 0) {
    http.end();
    return false;
  }

  if (!i2sInit((int)sampleRate, bclkPin_, lrckPin_, doutPin_)) {
    http.end();
    return false;
  }

  const size_t inBufSize = 1024;
  uint8_t inBuf[inBufSize];
  uint8_t outBuf[inBufSize * 2];

  uint32_t remaining = dataSize;
  while (remaining > 0 && stream->connected()) {
    if (gacha) gacha->loop();
    size_t toRead = remaining > inBufSize ? inBufSize : remaining;
    size_t got = stream->readBytes(inBuf, toRead);
    if (got == 0) break;
    remaining -= (uint32_t)got;

    const uint8_t *src = inBuf;
    size_t outLen = 0;
    if (numChannels == 2) {
      for (size_t i = 0; i + 3 < got; i += 4) {
        int16_t l = (int16_t)readU16LE(src + i);
        int16_t r = (int16_t)readU16LE(src + i + 2);
        l = scaleSample(l, volume_);
        r = scaleSample(r, volume_);
        outBuf[outLen + 0] = (uint8_t)(l & 0xFF);
        outBuf[outLen + 1] = (uint8_t)((l >> 8) & 0xFF);
        outBuf[outLen + 2] = (uint8_t)(r & 0xFF);
        outBuf[outLen + 3] = (uint8_t)((r >> 8) & 0xFF);
        outLen += 4;
      }
    } else {
      for (size_t i = 0; i + 1 < got; i += 2) {
        int16_t s = (int16_t)readU16LE(src + i);
        s = scaleSample(s, volume_);
        outBuf[outLen + 0] = (uint8_t)(s & 0xFF);
        outBuf[outLen + 1] = (uint8_t)((s >> 8) & 0xFF);
        outBuf[outLen + 2] = (uint8_t)(s & 0xFF);
        outBuf[outLen + 3] = (uint8_t)((s >> 8) & 0xFF);
        outLen += 4;
      }
    }

    size_t written = 0;
    while (written < outLen) {
      size_t w = 0;
      if (i2s_write(I2S_NUM_0, outBuf + written, outLen - written, &w, 100 / portTICK_PERIOD_MS) != ESP_OK) break;
      written += w;
      if (gacha) gacha->loop();
    }
  }

  i2s_zero_dma_buffer(I2S_NUM_0);
  i2s_driver_uninstall(I2S_NUM_0);
  http.end();
  return remaining == 0;
}

struct PlayArgs {
  char *url;
  AudioPlayer *self;
};

void audioTask(void *pv) {
  PlayArgs *args = (PlayArgs *)pv;
  AudioPlayer *self = args->self;
  bool ok = false;
  if (args->url && self) {
    ok = self->playWav("", String(args->url), nullptr);
  }
  if (args->url) free(args->url);
  free(args);
  if (self) {
    self->playing_ = false;
    self->task_ = nullptr;
  }
  (void)ok;
  vTaskDelete(nullptr);
}

bool AudioPlayer::playWavAsync(const char *baseUrl, const String &audioUrlOrPath) {
  if (!enabled_) return false;
  if (playing_) return false;
  String url = joinUrl(baseUrl, audioUrlOrPath);
  if (!url.length()) return false;

  PlayArgs *args = (PlayArgs *)calloc(1, sizeof(PlayArgs));
  if (!args) return false;
  args->self = this;
  args->url = strdup(url.c_str());
  if (!args->url) {
    free(args);
    return false;
  }

  playing_ = true;
  BaseType_t ok = xTaskCreatePinnedToCore(audioTask, "aiw_audio", 8192, args, 3, &task_, 1);
  if (ok != pdPASS) {
    playing_ = false;
    free(args->url);
    free(args);
    task_ = nullptr;
    return false;
  }
  return true;
}

bool AudioPlayer::isPlaying() const {
  return playing_;
}

void AudioPlayer::stop() {
  if (!task_) return;
  vTaskDelete(task_);
  task_ = nullptr;
  playing_ = false;
}

}  // namespace aiw

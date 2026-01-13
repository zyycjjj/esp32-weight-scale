#include "app/audio_player.h"

#include "app/gacha_controller.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <driver/i2s.h>
#include <math.h>
#include <Wire.h>

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

static bool g_i2sInstalled = false;

static bool i2cWriteReg(uint8_t addr, uint8_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission(true) == 0;
}

static bool i2cReadReg(uint8_t addr, uint8_t reg, uint8_t &val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(true) != 0) return false;
  if (Wire.requestFrom((int)addr, 1) != 1) return false;
  val = (uint8_t)Wire.read();
  return true;
}

static bool es8311Probe(uint8_t addr, uint8_t &id1, uint8_t &id2) {
  if (!i2cReadReg(addr, 0xFD, id1)) return false;
  if (!i2cReadReg(addr, 0xFE, id2)) return false;
  return true;
}

struct EsCoeff {
  uint32_t mclk;
  uint32_t rate;
  uint8_t pre_div;
  uint8_t pre_multi;
  uint8_t adc_div;
  uint8_t dac_div;
  uint8_t fs_mode;
  uint8_t lrck_h;
  uint8_t lrck_l;
  uint8_t bclk_div;
  uint8_t adc_osr;
  uint8_t dac_osr;
};

static const EsCoeff *es8311CoeffFor(uint32_t rate) {
  static const EsCoeff k[] = {
      {2048000, 8000, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xFF, 0x04, 0x10, 0x20},
      {4096000, 16000, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xFF, 0x04, 0x10, 0x20},
      {8192000, 32000, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xFF, 0x04, 0x10, 0x10},
      {11289600, 44100, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xFF, 0x04, 0x10, 0x10},
      {12288000, 48000, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xFF, 0x04, 0x10, 0x10},
  };
  uint32_t mclk = rate * 256;
  for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
    if (k[i].rate == rate && k[i].mclk == mclk) return &k[i];
  }
  return nullptr;
}

static bool es8311ConfigSample(uint8_t addr, uint32_t rate) {
  const EsCoeff *c = es8311CoeffFor(rate);
  if (!c) return false;

  uint8_t regv = 0;
  if (!i2cReadReg(addr, 0x02, regv)) return false;
  regv &= 0x07;
  regv |= (uint8_t)((c->pre_div - 1) << 5);
  regv |= (uint8_t)(c->pre_multi << 3);
  if (!i2cWriteReg(addr, 0x02, regv)) return false;

  regv = (uint8_t)(((c->adc_div - 1) << 4) | ((c->dac_div - 1) << 0));
  if (!i2cWriteReg(addr, 0x05, regv)) return false;

  regv = (uint8_t)((c->fs_mode << 6) | (c->adc_osr & 0x3F));
  if (!i2cWriteReg(addr, 0x03, regv)) return false;

  if (!i2cWriteReg(addr, 0x04, (uint8_t)(c->dac_osr & 0x3F))) return false;

  if (!i2cReadReg(addr, 0x07, regv)) return false;
  regv &= 0xC0;
  regv |= (uint8_t)(c->lrck_h & 0x3F);
  if (!i2cWriteReg(addr, 0x07, regv)) return false;
  if (!i2cWriteReg(addr, 0x08, c->lrck_l)) return false;

  if (!i2cReadReg(addr, 0x06, regv)) return false;
  regv &= 0xE0;
  if (c->bclk_div < 19) {
    regv |= (uint8_t)((c->bclk_div - 1) & 0x1F);
  } else {
    regv |= (uint8_t)(c->bclk_div & 0x1F);
  }
  if (!i2cWriteReg(addr, 0x06, regv)) return false;

  return true;
}

static bool es8311SetDacVolume(uint8_t addr, int volume0to100) {
  if (volume0to100 < 0) volume0to100 = 0;
  if (volume0to100 > 100) volume0to100 = 100;
  uint8_t reg32 = 0;
  if (volume0to100 != 0) {
    reg32 = (uint8_t)(((volume0to100 * 256) / 100) - 1);
  }
  return i2cWriteReg(addr, 0x32, reg32);
}

static bool es8311SetDacMute(uint8_t addr, bool mute) {
  uint8_t reg31 = 0;
  if (!i2cReadReg(addr, 0x31, reg31)) return false;
  if (mute) {
    reg31 |= (1u << 6) | (1u << 5);
  } else {
    reg31 &= ~((1u << 6) | (1u << 5));
  }
  return i2cWriteReg(addr, 0x31, reg31);
}

static int volumeToDacPercent(int volume) {
  if (volume <= 0) return 0;
  if (volume >= 21) return 100;
  return (volume * 100) / 21;
}

static bool es8311Init(uint8_t addr, uint32_t rate, bool mclkFromPin, uint32_t mclkHz) {
  uint8_t id1 = 0, id2 = 0;
  if (!es8311Probe(addr, id1, id2)) return false;

  if (!i2cWriteReg(addr, 0x00, 0x1F)) return false;
  delay(20);
  if (!i2cWriteReg(addr, 0x00, 0x00)) return false;
  if (!i2cWriteReg(addr, 0x00, 0x80)) return false;

  uint8_t reg01 = 0x3F;
  if (!mclkFromPin) reg01 |= (1u << 7);
  if (!i2cWriteReg(addr, 0x01, reg01)) return false;

  uint8_t reg00 = 0;
  if (!i2cReadReg(addr, 0x00, reg00)) return false;
  reg00 &= 0xBF;
  if (!i2cWriteReg(addr, 0x00, reg00)) return false;

  uint8_t reg09 = 0;
  uint8_t reg0a = 0;
  reg09 |= (3u << 2);
  reg0a |= (3u << 2);
  if (!i2cWriteReg(addr, 0x09, reg09)) return false;
  if (!i2cWriteReg(addr, 0x0A, reg0a)) return false;

  if (!es8311ConfigSample(addr, rate)) return false;

  if (!i2cWriteReg(addr, 0x0D, 0x01)) return false;
  if (!i2cWriteReg(addr, 0x0E, 0x02)) return false;
  if (!i2cWriteReg(addr, 0x12, 0x00)) return false;
  if (!i2cWriteReg(addr, 0x13, 0x10)) return false;
  if (!i2cWriteReg(addr, 0x1C, 0x6A)) return false;
  if (!i2cWriteReg(addr, 0x37, 0x08)) return false;

  if (!es8311SetDacMute(addr, true)) return false;
  (void)mclkHz;
  return true;
}

void AudioPlayer::begin(bool enabled, int bclkPin, int lrckPin, int doutPin, int mclkPin, int paCtrlPin, int i2cSdaPin, int i2cSclPin, int codecI2cAddr, int volume) {
  enabled_ = enabled && bclkPin >= 0 && lrckPin >= 0 && doutPin >= 0;
  bclkPin_ = bclkPin;
  lrckPin_ = lrckPin;
  doutPin_ = doutPin;
  mclkPin_ = mclkPin;
  paCtrlPin_ = paCtrlPin;
  i2cSdaPin_ = i2cSdaPin;
  i2cSclPin_ = i2cSclPin;
  codecI2cAddr_ = codecI2cAddr;
  volume_ = volume;
  playing_ = false;
  codecReady_ = false;
  if (enabled_ && paCtrlPin_ >= 0) {
    pinMode(paCtrlPin_, OUTPUT);
    digitalWrite(paCtrlPin_, LOW);
  }
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

static bool i2sInit(int sampleRate, int bclkPin, int lrckPin, int doutPin, int mclkPin) {
  if (g_i2sInstalled) {
    i2s_driver_uninstall(I2S_NUM_0);
    g_i2sInstalled = false;
  }

  i2s_config_t i2sConfig{};
  i2sConfig.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  i2sConfig.sample_rate = sampleRate;
  i2sConfig.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  i2sConfig.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  i2sConfig.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  i2sConfig.intr_alloc_flags = 0;
  i2sConfig.dma_buf_count = 8;
  i2sConfig.dma_buf_len = 256;
  i2sConfig.use_apll = (mclkPin >= 0);
  i2sConfig.tx_desc_auto_clear = true;
  i2sConfig.fixed_mclk = (mclkPin >= 0) ? (sampleRate * 256) : 0;

  if (i2s_driver_install(I2S_NUM_0, &i2sConfig, 0, nullptr) != ESP_OK) {
    return false;
  }
  g_i2sInstalled = true;

  i2s_pin_config_t pins{};
  pins.mck_io_num = (mclkPin >= 0) ? mclkPin : I2S_PIN_NO_CHANGE;
  pins.bck_io_num = bclkPin;
  pins.ws_io_num = lrckPin;
  pins.data_out_num = doutPin;
  pins.data_in_num = I2S_PIN_NO_CHANGE;
  if (i2s_set_pin(I2S_NUM_0, &pins) != ESP_OK) {
    i2s_driver_uninstall(I2S_NUM_0);
    g_i2sInstalled = false;
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

  if (!codecReady_) {
    if (i2cSdaPin_ >= 0 && i2cSclPin_ >= 0) {
      pinMode(i2cSdaPin_, INPUT_PULLUP);
      pinMode(i2cSclPin_, INPUT_PULLUP);
      Wire.begin(i2cSdaPin_, i2cSclPin_);
      Wire.setClock(100000);
      Wire.setTimeout(20);
      bool mclkFromPin = (mclkPin_ >= 0);
      uint32_t mclkHz = mclkFromPin ? ((uint32_t)sampleRate * 256u) : ((uint32_t)sampleRate * 32u);
      codecReady_ = es8311Init((uint8_t)codecI2cAddr_, (uint32_t)sampleRate, mclkFromPin, mclkHz);
      if (codecReady_) {
        es8311SetDacVolume((uint8_t)codecI2cAddr_, volumeToDacPercent(volume_));
        es8311SetDacMute((uint8_t)codecI2cAddr_, true);
      }
    }
  } else {
    es8311ConfigSample((uint8_t)codecI2cAddr_, (uint32_t)sampleRate);
    es8311SetDacVolume((uint8_t)codecI2cAddr_, volumeToDacPercent(volume_));
    es8311SetDacMute((uint8_t)codecI2cAddr_, true);
  }

  if (paCtrlPin_ >= 0) {
    digitalWrite(paCtrlPin_, HIGH);
    delay(20);
  }
  es8311SetDacMute((uint8_t)codecI2cAddr_, false);

  if (!i2sInit((int)sampleRate, bclkPin_, lrckPin_, doutPin_, mclkPin_)) {
    es8311SetDacMute((uint8_t)codecI2cAddr_, true);
    if (paCtrlPin_ >= 0) digitalWrite(paCtrlPin_, LOW);
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

  es8311SetDacMute((uint8_t)codecI2cAddr_, true);
  delay(10);
  i2s_zero_dma_buffer(I2S_NUM_0);
  i2s_driver_uninstall(I2S_NUM_0);
  g_i2sInstalled = false;
  if (paCtrlPin_ >= 0) digitalWrite(paCtrlPin_, LOW);
  http.end();
  return remaining == 0;
}

bool AudioPlayer::playBeep(int freqHz, int ms) {
  if (!enabled_) return false;
  if (freqHz <= 0 || ms <= 0) return false;

  const int sampleRate = 16000;

  if (!codecReady_) {
    if (i2cSdaPin_ >= 0 && i2cSclPin_ >= 0) {
      pinMode(i2cSdaPin_, INPUT_PULLUP);
      pinMode(i2cSclPin_, INPUT_PULLUP);
      Wire.begin(i2cSdaPin_, i2cSclPin_);
      Wire.setClock(100000);
      Wire.setTimeout(20);
      bool mclkFromPin = (mclkPin_ >= 0);
      uint32_t mclkHz = mclkFromPin ? ((uint32_t)sampleRate * 256u) : ((uint32_t)sampleRate * 32u);
      codecReady_ = es8311Init((uint8_t)codecI2cAddr_, (uint32_t)sampleRate, mclkFromPin, mclkHz);
      if (codecReady_) {
        es8311SetDacVolume((uint8_t)codecI2cAddr_, volumeToDacPercent(volume_));
        es8311SetDacMute((uint8_t)codecI2cAddr_, true);
      }
    }
  } else {
    es8311ConfigSample((uint8_t)codecI2cAddr_, (uint32_t)sampleRate);
    es8311SetDacVolume((uint8_t)codecI2cAddr_, volumeToDacPercent(volume_));
    es8311SetDacMute((uint8_t)codecI2cAddr_, true);
  }

  if (paCtrlPin_ >= 0) {
    digitalWrite(paCtrlPin_, HIGH);
    delay(20);
  }
  es8311SetDacMute((uint8_t)codecI2cAddr_, false);

  if (!i2sInit(sampleRate, bclkPin_, lrckPin_, doutPin_, mclkPin_)) return false;

  const int totalSamples = (sampleRate * ms) / 1000;
  const int chunkSamples = 256;
  int16_t buf[chunkSamples * 2];
  float phase = 0.0f;
  const float step = 2.0f * 3.1415926f * (float)freqHz / (float)sampleRate;

  int sent = 0;
  while (sent < totalSamples) {
    int n = totalSamples - sent;
    if (n > chunkSamples) n = chunkSamples;
    for (int i = 0; i < n; ++i) {
      float s = sinf(phase);
      phase += step;
      if (phase > 2.0f * 3.1415926f) phase -= 2.0f * 3.1415926f;
      int16_t v = scaleSample((int16_t)(s * 12000), volume_);
      buf[i * 2 + 0] = v;
      buf[i * 2 + 1] = v;
    }
    size_t bytes = (size_t)n * 2 * sizeof(int16_t);
    size_t w = 0;
    esp_err_t err = i2s_write(I2S_NUM_0, (const char *)buf, bytes, &w, 200 / portTICK_PERIOD_MS);
    if (err != ESP_OK) break;
    sent += n;
  }

  es8311SetDacMute((uint8_t)codecI2cAddr_, true);
  delay(10);
  i2s_zero_dma_buffer(I2S_NUM_0);
  i2s_driver_uninstall(I2S_NUM_0);
  g_i2sInstalled = false;
  if (paCtrlPin_ >= 0) digitalWrite(paCtrlPin_, LOW);
  return sent > 0;
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

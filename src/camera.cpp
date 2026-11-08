// HAL's front camera. Pin map and the I2C hand-over come from M5Stack's M5CoreS3
// GC0308 driver; see docs/superpowers/specs/2026-11-06-hal9000-camera-design.md.
#include "camera.h"

#include <string.h>

#include "esp_camera.h"
#include "hal_motion.h"

namespace {

constexpr int kW = 160, kH = 120;       // camera frame (QQVGA)
constexpr int kGW = 80, kGH = 60;       // preview / analysis grid
constexpr uint32_t kPollMs = 66;        // ~15 fps
constexpr int kCellChange = 25;         // grey-level change that counts a cell as moving
constexpr int kMinMovingCells = 12;

M5Canvas preview(&M5.Display);
uint16_t* frameCopy = nullptr;  // PSRAM, kW * kH
uint8_t prevGrey[kGW * kGH];
bool havePrev = false;
uint32_t lastPollMs = 0;
int emptyPolls = 0;
bool stalled = false;  // the camera stopped delivering frames: stop asking
int score = 0;
bool haveBox = false;
int boxX = 0, boxY = 0, boxW = 0, boxH = 0;

camera_config_t config() {
  camera_config_t c = {};
  c.pin_pwdn = -1;
  c.pin_reset = -1;
  c.pin_xclk = -1;
  c.pin_sccb_sda = 12;
  c.pin_sccb_scl = 11;
  c.pin_d7 = 47;
  c.pin_d6 = 48;
  c.pin_d5 = 16;
  c.pin_d4 = 15;
  c.pin_d3 = 42;
  c.pin_d2 = 41;
  c.pin_d1 = 40;
  c.pin_d0 = 39;
  c.pin_vsync = 46;
  c.pin_href = 38;
  c.pin_pclk = 45;
  c.xclk_freq_hz = 20000000;
  c.ledc_timer = LEDC_TIMER_0;
  c.ledc_channel = LEDC_CHANNEL_0;
  c.pixel_format = PIXFORMAT_RGB565;
  c.frame_size = FRAMESIZE_QQVGA;
  c.jpeg_quality = 0;
  c.fb_count = 2;
  c.fb_location = CAMERA_FB_IN_PSRAM;
  c.grab_mode = CAMERA_GRAB_LATEST;
  c.sccb_i2c_port = -1;
  return c;
}

// Byte-swapped RGB565 -> grey 0..255.
inline uint8_t grey(uint16_t swapped) {
  uint16_t v = (swapped >> 8) | (swapped << 8);
  int r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
  return static_cast<uint8_t>((r * 8 * 2 + g * 4 * 4 + b * 8) / 7);
}

}  // namespace

bool cameraBegin() {
  M5.In_I2C.release();  // the camera's SCCB takes the internal I2C pins during init
  camera_config_t c = config();
  esp_err_t err = esp_camera_init(&c);
  M5.In_I2C.begin();  // hand the bus back to touch, IMU, RTC and proximity
  Serial.printf("  camera: %s (0x%x)\n", err == ESP_OK ? "ok" : "failed", err);
  if (err != ESP_OK) return false;
  preview.setPsram(false);
  preview.setColorDepth(16);
  frameCopy = static_cast<uint16_t*>(ps_malloc(kW * kH * 2));
  if (!preview.createSprite(kGW, kGH) || !frameCopy) {
    Serial.println("  camera: out of memory, turning it off");
    preview.deleteSprite();
    free(frameCopy);
    frameCopy = nullptr;
    esp_camera_deinit();
    return false;
  }
  preview.fillScreen(TFT_BLACK);
  return true;
}

bool cameraPoll(uint32_t nowMs) {
  if (stalled || nowMs - lastPollMs < kPollMs) return false;
  lastPollMs = nowMs;
  camera_fb_t* fb = esp_camera_fb_get();  // can block for seconds if the camera stalls
  if (!fb) {
    if (++emptyPolls >= 3) {
      stalled = true;
      Serial.println("camera: no frames, giving up until restart");
    }
    return false;
  }
  emptyPolls = 0;
  if (fb->width != kW || fb->height != kH || fb->len < size_t(kW * kH * 2)) {
    esp_camera_fb_return(fb);
    return false;
  }
  const uint16_t* src = reinterpret_cast<const uint16_t*>(fb->buf);
  memcpy(frameCopy, src, kW * kH * 2);
  esp_camera_fb_return(fb);

  auto* dst = static_cast<uint16_t*>(preview.getBuffer());  // sprite buffers are byte-swapped too
  int moving = 0, minX = kGW, minY = kGH, maxX = -1, maxY = -1;
  for (int y = 0; y < kGH; ++y) {
    for (int x = 0; x < kGW; ++x) {
      uint16_t px = frameCopy[(y * 2) * kW + x * 2];
      dst[y * kGW + x] = px;
      uint8_t g = grey(px);
      int i = y * kGW + x;
      int d = havePrev ? abs(int(g) - int(prevGrey[i])) : 0;
      if (d > kCellChange) {
        ++moving;
        if (x < minX) minX = x;
        if (y < minY) minY = y;
        if (x > maxX) maxX = x;
        if (y > maxY) maxY = y;
      }
      prevGrey[i] = g;
    }
  }
  score = havePrev ? hal::motionScore(moving, kGW * kGH) : 0;
  havePrev = true;
  haveBox = moving >= kMinMovingCells;
  if (haveBox) {
    boxX = minX * 2;
    boxY = minY * 2;
    boxW = (maxX - minX + 1) * 2;
    boxH = (maxY - minY + 1) * 2;
  }
  return true;
}

M5Canvas& cameraPreview() { return preview; }
int cameraScore() { return score; }

bool cameraBox(int* x, int* y, int* w, int* h) {
  if (!haveBox) return false;
  *x = boxX;
  *y = boxY;
  *w = boxW;
  *h = boxH;
  return true;
}

const uint16_t* cameraFrame() { return havePrev ? frameCopy : nullptr; }

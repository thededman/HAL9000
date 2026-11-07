// HAL 9000 for the M5Stack CoreS3 — first boot: voice from the SD card, plain eye.
#include <M5Unified.h>
#include <SD.h>
#include <SPI.h>

#include "audio.h"
#include "hal_voice_index.h"
#include "sd_voice.h"

namespace {

Audio audio;
hal::VoiceIndex voices(esp_random());
hal::Config config;
bool haveMemory = false;

bool mountSd() {
  int cs = M5.getPin(m5::pin_name_t::sd_spi_cs);
  if (cs < 0) cs = GPIO_NUM_4;
  SPI.begin(M5.getPin(m5::pin_name_t::sd_spi_sclk), M5.getPin(m5::pin_name_t::sd_spi_miso),
            M5.getPin(m5::pin_name_t::sd_spi_mosi), cs);
  return SD.begin(cs, SPI, 25000000);
}

void say(hal::Folder f) {
  std::string path;
  if (voices.pick(f, &path)) audio.playFile(path.c_str());
}

void drawPlainEye() {
  auto& d = M5.Display;
  d.fillScreen(TFT_BLACK);
  d.fillCircle(160, 132, 92, d.color888(60, 60, 64));
  d.fillCircle(160, 132, 78, d.color888(150, 0, 0));
  d.fillCircle(160, 132, 12, d.color888(255, 220, 120));
  d.setTextColor(TFT_WHITE);
  d.setTextDatum(top_center);
  d.drawString("HAL 9000", 160, 14, &fonts::FreeSansBold12pt7b);
  if (!haveMemory) {
    d.setTextDatum(bottom_center);
    d.drawString("I can't find my memory, Dave.", 160, 236, &fonts::FreeSans9pt7b);
  }
}

}  // namespace

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  for (uint32_t t0 = millis(); !Serial && millis() - t0 < 2000;) delay(10);  // let a USB monitor attach
  Serial.println("\nHAL 9000 booting");

  if (mountSd()) {
    config = loadConfig();
    haveMemory = loadVoiceIndex(voices);
  } else {
    Serial.println("no SD card");
  }
  audio.setVolume(config.volume);
  drawPlainEye();
  if (haveMemory) say(hal::Folder::Boot);
}

void loop() {
  M5.update();
  if (haveMemory && M5.Touch.getDetail().wasClicked()) say(hal::Folder::Tap);
  delay(10);
}

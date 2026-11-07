// HAL 9000 for the M5Stack CoreS3 — voice from the SD card and the breathing eye.
#include <M5Unified.h>
#include <SD.h>
#include <SPI.h>

#include "audio.h"
#include "consoles.h"
#include "debug_shot.h"
#include "eye.h"
#include "hal_voice_index.h"
#include "sd_voice.h"

namespace {

Audio audio;
hal::VoiceIndex voices(esp_random());
hal::Config config;
bool haveMemory = false;
M5Canvas lens(&M5.Display);
M5Canvas screen(&M5.Display);
Consoles consoles;
bool inConsole = false;
uint32_t consoleStartMs = 0;
uint32_t lastFrameMs = 0, fpsFrames = 0, fpsStartMs = 0;

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

void startConsole(bool speak) {
  consoles.next();
  inConsole = true;
  consoleStartMs = millis();
  if (speak) say(hal::Folder::Tap);
}

void drawFrame() {
  uint32_t now = millis();
  if (inConsole && now - consoleStartMs >= 8000 && !audio.isPlaying()) {
    inConsole = false;
    drawEyeBackground(M5.Display, !haveMemory);
  }
  if (inConsole) {
    if (consoles.draw(screen, now - consoleStartMs, audio.level())) screen.pushSprite(0, 0);
  } else {
    hal::Mode m = audio.isPlaying() ? hal::Mode::Speaking : hal::Mode::Idle;
    drawEye(lens, eyeParamsFor(m, false, 0, audio.level(), now));
    lens.pushSprite(kEyeCx - kLensSprite / 2, kEyeCy - kLensSprite / 2, eyeTransparentKey());
  }
  if (++fpsFrames, now - fpsStartMs >= 5000) {
    Serial.printf("fps %.1f\n", fpsFrames * 1000.0f / (now - fpsStartMs));
    fpsFrames = 0;
    fpsStartMs = now;
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
  M5.Display.setBrightness(160);
  audio.setVolume(config.volume);
  lens.setPsram(false);  // 46 KB in internal RAM: faster to draw and push
  lens.setColorDepth(16);
  lens.createSprite(kLensSprite, kLensSprite);
  screen.setPsram(false);  // 75 KB in internal RAM
  screen.setColorDepth(8);  // RGB332 is plenty for the console readouts
  screen.createSprite(320, 240);
  consoles.begin(esp_random());
  drawEyeBackground(M5.Display, !haveMemory);
  if (haveMemory) say(hal::Folder::Boot);
}

void loop() {
  M5.update();
  int cmd = serviceDebugCommands();
  if (cmd == 'C') startConsole(false);  // debug: next console screen, silent
  if (cmd == 'T') startConsole(haveMemory);  // debug: same as a tap
  if (M5.Touch.getDetail().wasClicked()) startConsole(haveMemory);
  if (millis() - lastFrameMs >= 33) {
    lastFrameMs = millis();
    drawFrame();
  }
  delay(1);
}

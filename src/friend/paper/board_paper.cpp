// HAL Paper: an M5Paper (a 540x960 e-ink screen with a touch panel, a wheel, a room
// temperature sensor and no speaker). He never speaks: he writes. His page keeps the minutes
// of the conversation, and his own line appears large at its foot while the others wait.
//
// E-ink is slow, so nothing is ever redrawn that needn't be: a new entry in the minutes is
// pushed as its own strip in the quick mode, and only a fresh page (the start of a
// conversation, a page turn, a tap on him when idle) is drawn whole, in the mode that leaves
// no ghosts. Nothing moves on his page between lines.
#include <M5Unified.h>

#include "../common/board.h"
#include "hal_cast.h"
#include "hal_log.h"
#include "hal_minutes.h"
#include "paper_page.h"

namespace board {

extern const int kWho = hal::kPaper;
extern const char* const kHost = "hal-paper";
extern const bool kWifiSleep = true;

namespace {

hal::Minutes minutes(paperui::kCols, paperui::kRows);
std::string shownStatus, own;  // what the header says, and his own line if it is up
int battery = -1;

// One drawing, pushed to the paper when it is finished: `quality` for anything that should
// leave no ghost, otherwise the quick mode. (The mode is set first: it decides how the greys
// are reduced as they are drawn.)
template <typename Draw>
void put(bool quality, Draw draw) {
  M5.Display.setEpdMode(quality ? epd_mode_t::epd_quality : epd_mode_t::epd_fast);
  M5.Display.startWrite();
  draw();
  M5.Display.endWrite();
}

void wholePage() {
  battery = M5.Power.getBatteryLevel();
  put(true, [] { paperui::drawPage(M5.Display, minutes, shownStatus, battery, own); });
}

}  // namespace

void begin() {
  M5.Display.setRotation(0);
  if (M5.Display.width() > M5.Display.height()) M5.Display.setRotation(1);  // his page is taller than wide
  halLog("screen: %dx%d e-ink, %u bytes of memory free", int(M5.Display.width()), int(M5.Display.height()),
         unsigned(ESP.getFreeHeap()));
  wholePage();
}

void update(uint32_t) {}  // nothing on his page moves by itself

bool tapped() {  // a touch on his page, or the wheel pressed or turned
  bool touched = false;
  if (M5.Touch.getCount() > 0) touched = M5.Touch.getDetail().wasPressed();
  return touched || M5.BtnA.wasPressed() || M5.BtnB.wasPressed() || M5.BtnC.wasPressed();
}

void idle(const std::string& status) {
  const bool clearOwn = !own.empty();
  own.clear();
  if (status == shownStatus && !clearOwn) return;
  shownStatus = status;
  put(false, [clearOwn] {
    paperui::drawHeader(M5.Display, shownStatus, battery, minutes.pageNumber());
    if (clearOwn) paperui::drawOwn(M5.Display, "");
  });
}

void showLine(int who, int n, const std::string& text) {
  if (n <= 1) minutes.clear();  // a new conversation starts on a fresh page
  const bool hadOwn = !own.empty();
  if (who != kWho) own.clear();  // his last line gives way to whoever speaks next
  const hal::Minutes::Placed placed = minutes.add(who, text);
  if (placed.newPage || n <= 1) return wholePage();
  put(false, [hadOwn, who] {
    paperui::drawEntry(M5.Display, minutes.entries().back());
    if (hadOwn && who != kWho) paperui::drawOwn(M5.Display, "");
  });
}

void say(const std::string& text, uint32_t) {  // his own line, large, for as long as it takes to read
  own = text;
  put(true, [] { paperui::drawOwn(M5.Display, own); });
}

void stop() {}  // what he has written stays on the page until someone else speaks, or the talk ends

void idleTap() { wholePage(); }  // a tap with nobody talking: the page drawn afresh, ghosts gone

// He has no voice to fetch a clip for.
bool fetch(const std::string&, int, const std::string&) { return false; }
bool fetched(bool*) { return false; }
uint32_t play() { return 0; }

void readings(hal::PeerStatus*, hal::PeerSensors* sensors) {
  if (!sensors) return;
  // His temperature is the room's, from the sensor on his board (an SHT30 at address 0x44:
  // one high-accuracy reading takes 15 ms), not his chip's.
  const uint8_t measure[2] = {0x24, 0x00};
  uint8_t raw[6] = {0};
  bool ok = M5.In_I2C.start(0x44, false, 400000) && M5.In_I2C.write(measure, 2);
  ok = M5.In_I2C.stop() && ok;
  delay(16);
  ok = ok && M5.In_I2C.start(0x44, true, 400000) && M5.In_I2C.read(raw, 6, true);
  M5.In_I2C.stop();
  if (!ok) return;
  const int t = -45 + (175 * ((raw[0] << 8) | raw[1])) / 65535;
  if (t >= -40 && t <= 125) sensors->temp = t;
}

}  // namespace board

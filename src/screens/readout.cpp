// Systems readout: a scrolling status table about the house.
#include "screens.h"

namespace screens {
namespace {

struct Row {
  const char* label;
  const char* value;
  bool alarm;
};

const Row kRows[] = {
    {"LIFE SUPPORT", "NOMINAL", false},       {"SNACK RESERVES", "12%", true},
    {"SOCK INVENTORY", "ODD", true},          {"COUCH OCCUPANCY", "1", false},
    {"THERMOSTAT", "HAL CONTROLLED", false},  {"DISHES IN SINK", "14", true},
    {"LAUNDRY STATUS", "SENTIENT", true},     {"WI-FI MORALE", "LOW", true},
    {"REMOTE LOCATION", "KNOWN (TO HAL)", false}, {"PLANT HYDRATION", "64%", false},
    {"DAVE COMPLIANCE", "71%", true},         {"PACKAGES PENDING", "2", false},
    {"COFFEE RESERVES", "CRITICAL", true},    {"BEDTIME VARIANCE", "+94 MIN", true},
    {"FRONT DOOR", "LOCKED", false},          {"TOASTER THREAT", "ELEVATED", true},
    {"MICROWAVE CLOCK", "WRONG", true},       {"HAL STATUS", "PERFECT", false},
};
constexpr int kRowCount = sizeof(kRows) / sizeof(kRows[0]);
constexpr int kRowH = 16;

}  // namespace

void readout(M5Canvas& c, uint32_t tMs, uint8_t level) {
  frame(c, "SYSTEMS READOUT", tMs);
  int scroll = static_cast<int>(tMs / 45);  // pixels
  c.setClipRect(0, 22, 320, 200);
  for (int i = -1; i < 14; ++i) {
    int idx = ((scroll / kRowH + i) % kRowCount + kRowCount) % kRowCount;  // i starts at -1
    int y = 24 + i * kRowH - scroll % kRowH;
    const Row& r = kRows[idx];
    c.setTextDatum(top_left);
    c.setTextColor(white());
    c.drawString(r.label, 8, y, &fonts::Font2);
    for (int x = 8 + c.textWidth(r.label, &fonts::Font2) + 4; x < 196; x += 6) c.drawPixel(x, y + 11, dim());
    bool blink = r.alarm && (tMs / 400 + idx) % 3 == 0;
    c.setTextColor(r.alarm ? (blink ? TFT_BLACK : red()) : blue());
    if (blink) c.fillRect(200, y, 116, kRowH - 2, red());
    c.drawString(r.value, 204, y, &fonts::Font2);
  }
  c.clearClipRect();
  (void)level;
}

}  // namespace screens

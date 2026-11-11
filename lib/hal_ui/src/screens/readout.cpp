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

}  // namespace

void readout(M5Canvas& c, const hal::Layout& L, uint32_t tMs, uint8_t level) {
  frame(c, L, "SYSTEMS READOUT", tMs);
  const int kRowH = L.H(16);
  const lgfx::IFont* font = L.font2();
  int scroll = static_cast<int>(tMs / 45);  // pixels
  c.setClipRect(0, L.Y(22), L.w, L.H(200));
  const int rows = L.H(200) / kRowH + 2;  // 14 on the CoreS3, as before
  for (int i = -1; i < rows; ++i) {
    int idx = ((scroll / kRowH + i) % kRowCount + kRowCount) % kRowCount;  // i starts at -1
    int y = L.Y(24) + i * kRowH - scroll % kRowH;
    const Row& r = kRows[idx];
    c.setTextDatum(top_left);
    c.setTextColor(white());
    c.drawString(r.label, 8, y, font);
    for (int x = 8 + c.textWidth(r.label, font) + 4; x < 196; x += 6) c.drawPixel(x, y + kRowH - 5, dim());
    bool blink = r.alarm && (tMs / 400 + idx) % 3 == 0;
    c.setTextColor(r.alarm ? (blink ? TFT_BLACK : red()) : blue());
    if (blink) c.fillRect(200, y, 116, kRowH - 2, red());
    c.drawString(r.value, 204, y, font);
  }
  c.clearClipRect();
  (void)level;
}

}  // namespace screens

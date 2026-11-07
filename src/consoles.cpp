// SD.h must come before M5GFX so drawJpgFile(SD, ...) gets its file wrapper.
#include <SD.h>

#include "consoles.h"

#include <algorithm>

#include "screens/screens.h"

namespace {

using ScreenFn = void (*)(M5Canvas&, uint32_t, uint8_t);
const ScreenFn kBuiltIn[] = {screens::vitals, screens::readout, screens::trajectory, screens::analyser,
                             screens::globe};
constexpr int kBuiltInCount = sizeof(kBuiltIn) / sizeof(kBuiltIn[0]);

bool isJpg(const std::string& n) {
  if (n.size() < 5 || n[0] == '.') return false;
  std::string ext = n.substr(n.rfind('.') == std::string::npos ? 0 : n.rfind('.'));
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
  return ext == ".jpg" || ext == ".jpeg";
}

}  // namespace

void Consoles::begin(uint32_t seed) {
  bag_ = hal::ShuffleBag(seed);
  jpgs_.clear();
  File d = SD.open("/hal/screens");
  if (d && d.isDirectory()) {
    for (File f = d.openNextFile(); f; f = d.openNextFile()) {
      std::string n = f.name();
      if (!f.isDirectory() && isJpg(n)) jpgs_.push_back("/hal/screens/" + n);
      f.close();
    }
  }
  std::sort(jpgs_.begin(), jpgs_.end());
  bag_.reset(kBuiltInCount + jpgs_.size());
  Serial.printf("  console screens: %d built-in + %u images\n", kBuiltInCount, unsigned(jpgs_.size()));
  next();
}

void Consoles::next() {
  current_ = bag_.next();
  jpgDrawn_ = false;
}

bool Consoles::draw(M5Canvas& c, uint32_t tMs, uint8_t voiceLevel) {
  if (current_ < kBuiltInCount) {
    kBuiltIn[current_](c, tMs, voiceLevel);
    return true;
  }
  if (jpgDrawn_) return false;  // a still image: decode once, then hold it
  auto& d = M5.Display;
  d.fillScreen(TFT_BLACK);
  const std::string& path = jpgs_[current_ - kBuiltInCount];
  if (!d.drawJpgFile(SD, path.c_str(), 0, 0, 320, 240)) Serial.printf("skip %s: can't decode\n", path.c_str());
  jpgDrawn_ = true;
  return false;
}

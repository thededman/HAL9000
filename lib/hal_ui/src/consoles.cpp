#include "consoles.h"

#include <algorithm>
#include <cctype>

#include "hal_log.h"

namespace {

using ScreenFn = void (*)(M5Canvas&, const hal::Layout&, uint32_t, uint8_t);
const ScreenFn kBuiltIn[] = {screens::vitals, screens::readout, screens::trajectory, screens::analyser,
                             screens::globe};
constexpr int kBuiltInCount = sizeof(kBuiltIn) / sizeof(kBuiltIn[0]);

bool isJpg(const std::string& n) {
  if (n.size() < 5 || n[0] == '.') return false;
  std::string ext = n.substr(n.rfind('.') == std::string::npos ? 0 : n.rfind('.'));
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) { return std::tolower(ch); });
  return ext == ".jpg" || ext == ".jpeg";
}

}  // namespace

void Consoles::begin(uint32_t seed, bool withCamera, const hal::Storage* storage) {
  withCamera_ = withCamera;
  storage_ = storage;
  bag_ = hal::ShuffleBag(seed);
  jpgs_.clear();
  if (storage_) {
    for (const std::string& n : storage_->list("/hal/screens"))
      if (isJpg(n)) jpgs_.push_back("/hal/screens/" + n);
  }
  std::sort(jpgs_.begin(), jpgs_.end());
  bag_.reset(kBuiltInCount + (withCamera_ ? 1 : 0) + jpgs_.size());
  halLog("  console screens: %d built-in%s + %u images", kBuiltInCount, withCamera_ ? " + visual analysis" : "",
         unsigned(jpgs_.size()));
  next();
}

bool Consoles::next() {
  current_ = bag_.next();
  jpgDrawn_ = false;
  return withCamera_ && current_ == kBuiltInCount;
}

void Consoles::show(int index) {
  current_ = index;
  jpgDrawn_ = false;
}

int Consoles::screenCount() const { return kBuiltInCount + (withCamera_ ? 1 : 0) + int(jpgs_.size()); }

bool Consoles::showVisual() {
  if (!withCamera_) return false;
  show(kBuiltInCount);
  return true;
}

bool Consoles::draw(M5Canvas& c, lgfx::LovyanGFX& display, const hal::Layout& L, uint32_t tMs, uint8_t voiceLevel,
                    const screens::CameraView& cam) {
  if (current_ < kBuiltInCount) {
    kBuiltIn[current_](c, L, tMs, voiceLevel);
    return true;
  }
  if (withCamera_ && current_ == kBuiltInCount) {
    screens::visual(c, L, tMs, voiceLevel, cam);
    return true;
  }
  if (jpgDrawn_) return false;  // a still image: decode once, then hold it
  jpgDrawn_ = true;
  const std::string& path = jpgs_[current_ - kBuiltInCount - (withCamera_ ? 1 : 0)];
  std::vector<uint8_t> img;  // read first: on the CoreS3 the SD card shares the display's bus
  bool haveImg = storage_ && storage_->readAll(path, &img);
  display.fillScreen(TFT_BLACK);
  if (!haveImg || !display.drawJpg(img.data(), img.size(), 0, 0, L.w, L.h)) {
    halLog("skip %s: can't decode", path.c_str());
  }
  return false;
}

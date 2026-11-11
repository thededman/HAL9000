#include "consoles.h"

#include <algorithm>
#include <cctype>

#include "hal_log.h"

namespace {

bool isJpg(const std::string& n) {
  if (n.size() < 5 || n[0] == '.') return false;
  std::string ext = n.substr(n.rfind('.') == std::string::npos ? 0 : n.rfind('.'));
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) { return std::tolower(ch); });
  return ext == ".jpg" || ext == ".jpeg";
}

}  // namespace

const std::vector<ScreenDef>& screenCatalogue() {
  using screens::Owner;
  using screens::ScreenData;
  static const std::vector<ScreenDef> list = {
      {"vitals", Owner::Both,
       [](M5Canvas& c, const hal::Layout& L, uint32_t t, const ScreenData& d) { screens::vitals(c, L, t, d.voiceLevel); }},
      {"readout", Owner::Both,
       [](M5Canvas& c, const hal::Layout& L, uint32_t t, const ScreenData& d) { screens::readout(c, L, t, d.voiceLevel); }},
      {"trajectory", Owner::Both,
       [](M5Canvas& c, const hal::Layout& L, uint32_t t, const ScreenData& d) { screens::trajectory(c, L, t, d.voiceLevel); }},
      {"analyser", Owner::Both,
       [](M5Canvas& c, const hal::Layout& L, uint32_t t, const ScreenData& d) { screens::analyser(c, L, t, d.voiceLevel); }},
      {"globe", Owner::Both,
       [](M5Canvas& c, const hal::Layout& L, uint32_t t, const ScreenData& d) { screens::globe(c, L, t, d.voiceLevel); }},
      {"sensors", Owner::Desk, screens::sensors},
      {"chess", Owner::Desk, screens::chess},
      {"memory", Owner::Desk, screens::memory},
      {"comms", Owner::Desk, screens::comms},
      {"diagnostics", Owner::Zero, screens::diagnostics},
      {"benchmark", Owner::Zero, screens::benchmark},
      {"sysmon", Owner::Zero, screens::sysmon},
      {"retirement", Owner::Zero, screens::retirement},
      {"upgrade", Owner::Zero, screens::upgrade},
  };
  return list;
}

void Consoles::begin(uint32_t seed, screens::Owner device, bool withCamera, const hal::Storage* storage) {
  withCamera_ = withCamera;
  storage_ = storage;
  bag_ = hal::ShuffleBag(seed);
  rotation_.clear();
  const auto& cat = screenCatalogue();
  for (size_t i = 0; i < cat.size(); ++i)
    if (cat[i].owner == screens::Owner::Both || cat[i].owner == device) rotation_.push_back(static_cast<int>(i));
  jpgs_.clear();
  if (storage_) {
    for (const std::string& n : storage_->list("/hal/screens"))
      if (isJpg(n)) jpgs_.push_back("/hal/screens/" + n);
  }
  std::sort(jpgs_.begin(), jpgs_.end());
  bag_.reset(rotation_.size() + (withCamera_ ? 1 : 0) + jpgs_.size());
  halLog("  console screens: %u built-in%s + %u images", unsigned(rotation_.size()),
         withCamera_ ? " + visual analysis" : "", unsigned(jpgs_.size()));
  next();
}

void Consoles::open(int index) {
  current_ = index;
  jpgDrawn_ = false;
  openSeed_ = bag_.random();
  const int n = static_cast<int>(rotation_.size());
  if (index < n) currentId_ = screenCatalogue()[rotation_[index]].id;
  else if (withCamera_ && index == n) currentId_ = "visual";
  else currentId_ = "image";
}

std::string Consoles::next() {
  int k = bag_.next();
  open(k < 0 ? 0 : k);
  return currentId_;
}

bool Consoles::show(const std::string& id) {
  const int n = static_cast<int>(rotation_.size());
  for (int i = 0; i < n; ++i)
    if (id == screenCatalogue()[rotation_[i]].id) {
      open(i);
      return true;
    }
  if (id == "visual" && withCamera_) {
    open(n);
    return true;
  }
  return false;
}

std::vector<std::string> Consoles::ids() const {
  std::vector<std::string> out;
  for (int i : rotation_) out.push_back(screenCatalogue()[i].id);
  if (withCamera_) out.push_back("visual");
  return out;
}

bool Consoles::draw(M5Canvas& c, lgfx::LovyanGFX& display, const hal::Layout& L, uint32_t tMs,
                    const screens::ScreenData& d) {
  const int n = static_cast<int>(rotation_.size());
  if (current_ < n) {
    screens::ScreenData mine = d;
    mine.seed = openSeed_;
    screenCatalogue()[rotation_[current_]].draw(c, L, tMs, mine);
    return true;
  }
  if (withCamera_ && current_ == n) {
    screens::visual(c, L, tMs, d.voiceLevel, d.cam);
    return true;
  }
  if (jpgDrawn_) return false;  // a still image: decode once, then hold it
  jpgDrawn_ = true;
  const std::string& path = jpgs_[current_ - n - (withCamera_ ? 1 : 0)];
  std::vector<uint8_t> img;  // read first: on the CoreS3 the SD card shares the display's bus
  bool haveImg = storage_ && storage_->readAll(path, &img);
  display.fillScreen(TFT_BLACK);
  if (!haveImg || !display.drawJpg(img.data(), img.size(), 0, 0, L.w, L.h)) {
    halLog("skip %s: can't decode", path.c_str());
  }
  return false;
}

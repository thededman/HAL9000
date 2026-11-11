#include "hal_screen_voices.h"

#include <algorithm>

namespace hal {

void ScreenVoices::load(const Storage& storage, const std::vector<std::string>& screenIds) {
  entries_.clear();
  uint32_t n = 0;
  for (const std::string& id : screenIds) {
    const std::string dir = std::string(kScreenVoiceRoot) + "/" + id;
    std::vector<std::string> names;
    for (const std::string& name : storage.list(dir))
      if (isVoiceFile(name)) names.push_back(name);
    std::sort(names.begin(), names.end());
    Entry e;
    e.bag = ShuffleBag(seed_ + n++);
    for (const std::string& name : names) e.paths.push_back(dir + "/" + name);
    e.bag.reset(e.paths.size());
    entries_[id] = std::move(e);
  }
}

int ScreenVoices::count(const std::string& id) const {
  auto it = entries_.find(id);
  return it == entries_.end() ? 0 : static_cast<int>(it->second.paths.size());
}

bool ScreenVoices::pick(const std::string& id, std::string* path) {
  auto it = entries_.find(id);
  if (it == entries_.end() || it->second.paths.empty()) return false;
  int i = it->second.bag.next();
  if (i < 0) return false;
  *path = it->second.paths[i];
  return true;
}

bool pickConsoleLine(const std::string& screenId, ScreenVoices& screens, VoiceIndex& voices, std::string* path) {
  if (screenId == "visual" && voices.count(Folder::Camera) > 0) return voices.pick(Folder::Camera, path);
  if (screens.pick(screenId, path)) return true;
  return voices.pick(Folder::Tap, path);
}

}  // namespace hal

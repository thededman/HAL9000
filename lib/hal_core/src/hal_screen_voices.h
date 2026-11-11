#pragma once
// Lines that belong to one console screen: /hal/voice/screens/<id>/*.wav. When a
// tap brings up a screen that has its own lines, HAL says one of those.
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "hal_shuffle.h"
#include "hal_storage.h"
#include "hal_voice_index.h"

namespace hal {

constexpr const char* kScreenVoiceRoot = "/hal/voice/screens";

class ScreenVoices {
 public:
  explicit ScreenVoices(uint32_t seed) : seed_(seed) {}
  // Lists each screen's folder (voice files only, sorted); replaces what was loaded.
  void load(const Storage& storage, const std::vector<std::string>& screenIds);
  int count(const std::string& id) const;
  bool pick(const std::string& id, std::string* path);  // shuffled, no repeats; false if none

 private:
  struct Entry {
    std::vector<std::string> paths;
    ShuffleBag bag{1};
  };
  uint32_t seed_;
  std::map<std::string, Entry> entries_;
};

// The line for a console screen: a camera line for "visual", the screen's own
// lines if it has any, otherwise a tap line.
bool pickConsoleLine(const std::string& screenId, ScreenVoices& screens, VoiceIndex& voices, std::string* path);

}  // namespace hal

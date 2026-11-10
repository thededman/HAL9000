#include "hal_voice_index.h"

#include <algorithm>
#include <cctype>

namespace hal {

const char* folderPath(Folder f) {
  static const char* const kPaths[kFolderCount] = {
      "/hal/voice/boot",          "/hal/voice/reboot",         "/hal/voice/tap",
      "/hal/voice/greet/morning", "/hal/voice/greet/afternoon", "/hal/voice/greet/evening",
      "/hal/voice/shake",         "/hal/voice/idle",           "/hal/voice/plea",
      "/hal/voice/relief",        "/hal/voice/daisy",          "/hal/voice/camera",
      "/hal/voice/rival",
  };
  return kPaths[static_cast<int>(f)];
}

bool isVoiceFile(const std::string& name) {
  if (name.size() < 5 || name[0] == '.') return false;
  std::string ext = name.substr(name.size() - 4);
  for (char& ch : ext) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  return ext == ".wav";
}

VoiceIndex::VoiceIndex(uint32_t seed) {
  for (int i = 0; i < kFolderCount; ++i) entries_.push_back(Entry{{}, ShuffleBag(seed + 7919u * (i + 1))});
}

void VoiceIndex::setFiles(Folder f, const std::vector<std::string>& names) {
  entries_[static_cast<int>(f)].paths.clear();
  addFiles(f, folderPath(f), names);
}

void VoiceIndex::addFiles(Folder f, const std::string& dir, const std::vector<std::string>& names) {
  Entry& e = entries_[static_cast<int>(f)];
  for (const std::string& n : names) {
    if (isVoiceFile(n)) e.paths.push_back(dir + "/" + n);
  }
  std::sort(e.paths.begin(), e.paths.end());
  e.bag.reset(e.paths.size());
}

bool VoiceIndex::pick(Folder f, std::string* path) {
  Entry& e = entries_[static_cast<int>(f)];
  int i = e.bag.next();
  if (i < 0) return false;
  *path = e.paths[i];
  return true;
}

bool VoiceIndex::at(Folder f, int i, std::string* path) const {
  const Entry& e = entries_[static_cast<int>(f)];
  if (i < 0 || i >= static_cast<int>(e.paths.size())) return false;
  *path = e.paths[i];
  return true;
}

int VoiceIndex::count(Folder f) const { return static_cast<int>(entries_[static_cast<int>(f)].paths.size()); }

}  // namespace hal

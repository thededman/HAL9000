#pragma once
// Which WAV files HAL has in each SD folder, and which one to say next.
#include <cstdint>
#include <string>
#include <vector>

#include "hal_shuffle.h"

namespace hal {

enum class Folder { Boot, Reboot, Tap, GreetMorning, GreetAfternoon, GreetEvening, Shake, Idle, Plea, Relief, Daisy, Camera };
constexpr int kFolderCount = 12;

// "/hal/voice/greet/morning" etc.
const char* folderPath(Folder f);

// Ends in .wav (any case) and isn't a hidden/AppleDouble file.
bool isVoiceFile(const std::string& name);

class VoiceIndex {
 public:
  explicit VoiceIndex(uint32_t seed);
  // Replaces the folder's files; names are bare file names inside folderPath(f).
  void setFiles(Folder f, const std::vector<std::string>& names);
  // Adds files from another directory to the folder (used for /hal/voice/mine -> Tap).
  void addFiles(Folder f, const std::string& dir, const std::vector<std::string>& names);
  bool pick(Folder f, std::string* path);              // shuffled, no repeats
  bool at(Folder f, int i, std::string* path) const;  // name order (plea, daisy)
  int count(Folder f) const;

 private:
  struct Entry {
    std::vector<std::string> paths;
    ShuffleBag bag;
  };
  std::vector<Entry> entries_;
};

}  // namespace hal

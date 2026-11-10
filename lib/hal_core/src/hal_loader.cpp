#include "hal_loader.h"

namespace hal {

Config loadConfig(const Storage& storage, std::vector<std::string>* warnings) {
  std::vector<uint8_t> bytes;
  if (!storage.readAll("/hal/config.txt", &bytes)) {
    if (warnings) warnings->push_back("no /hal/config.txt, using defaults");
    return Config();
  }
  return parseConfig(std::string(bytes.begin(), bytes.end()), warnings);
}

bool loadVoiceIndex(const Storage& storage, VoiceIndex& index) {
  if (!storage.exists("/hal/voice")) return false;
  for (int i = 0; i < kFolderCount; ++i) {
    auto f = static_cast<Folder>(i);
    index.setFiles(f, storage.list(folderPath(f)));
  }
  index.addFiles(Folder::Tap, "/hal/voice/mine", storage.list("/hal/voice/mine"));
  return true;
}

}  // namespace hal

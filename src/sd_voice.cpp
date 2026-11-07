#include "sd_voice.h"

#include <Arduino.h>
#include <SD.h>

#include <string>
#include <vector>

namespace {

std::vector<std::string> listFiles(const char* dir) {
  std::vector<std::string> names;
  File d = SD.open(dir);
  if (!d || !d.isDirectory()) return names;
  for (File f = d.openNextFile(); f; f = d.openNextFile()) {
    if (!f.isDirectory()) names.push_back(f.name());
    f.close();
  }
  d.close();
  return names;
}

}  // namespace

bool loadVoiceIndex(hal::VoiceIndex& index) {
  if (!SD.exists("/hal/voice")) return false;
  for (int i = 0; i < hal::kFolderCount; ++i) {
    auto f = static_cast<hal::Folder>(i);
    index.setFiles(f, listFiles(hal::folderPath(f)));
  }
  index.addFiles(hal::Folder::Tap, "/hal/voice/mine", listFiles("/hal/voice/mine"));
  for (int i = 0; i < hal::kFolderCount; ++i) {
    auto f = static_cast<hal::Folder>(i);
    Serial.printf("  %-28s %d\n", hal::folderPath(f), index.count(f));
  }
  return true;
}

hal::Config loadConfig() {
  File f = SD.open("/hal/config.txt", FILE_READ);
  if (!f) {
    Serial.println("no /hal/config.txt, using defaults");
    return hal::Config();
  }
  std::string text;
  text.reserve(f.size());
  while (f.available()) text.push_back(static_cast<char>(f.read()));
  f.close();
  std::vector<std::string> warnings;
  hal::Config c = hal::parseConfig(text, &warnings);
  for (auto& w : warnings) Serial.printf("config: %s\n", w.c_str());
  return c;
}

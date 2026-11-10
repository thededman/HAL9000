#include "sd_storage.h"

#include <SD.h>

bool SdStorage::exists(const std::string& path) const { return SD.exists(path.c_str()); }

std::vector<std::string> SdStorage::list(const std::string& dir) const {
  std::vector<std::string> names;
  File d = SD.open(dir.c_str());
  if (!d || !d.isDirectory()) return names;
  for (File f = d.openNextFile(); f; f = d.openNextFile()) {
    if (!f.isDirectory()) names.push_back(f.name());
    f.close();
  }
  d.close();
  return names;
}

bool SdStorage::readAll(const std::string& path, std::vector<uint8_t>* out) const {
  File f = SD.open(path.c_str(), FILE_READ);
  if (!f) return false;
  out->resize(f.size());
  size_t got = f.read(out->data(), out->size());
  f.close();
  return got == out->size();
}

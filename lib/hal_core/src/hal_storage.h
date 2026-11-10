#pragma once
// Where HAL's files live: the SD card on the CoreS3, ~/hal on the Cardputer.
// Paths are always written "/hal/...", as on the SD card.
#include <cstdint>
#include <string>
#include <vector>

namespace hal {

class Storage {
 public:
  virtual ~Storage() = default;
  virtual bool exists(const std::string& path) const = 0;
  // Bare file names in a directory (no sub-directories); empty if it doesn't exist.
  virtual std::vector<std::string> list(const std::string& dir) const = 0;
  virtual bool readAll(const std::string& path, std::vector<uint8_t>* out) const = 0;
};

}  // namespace hal

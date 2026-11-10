#pragma once
// HAL's files on the CoreS3's SD card.
#include "hal_storage.h"

class SdStorage : public hal::Storage {
 public:
  bool exists(const std::string& path) const override;
  std::vector<std::string> list(const std::string& dir) const override;
  bool readAll(const std::string& path, std::vector<uint8_t>* out) const override;
};

#pragma once
// HAL's files on Linux and macOS: "/hal/x" lives at <root>/x (default ~/hal).
#include <string>

#include "hal_storage.h"

class PosixStorage : public hal::Storage {
 public:
  explicit PosixStorage(std::string root) : root_(std::move(root)) {}
  bool exists(const std::string& path) const override;
  std::vector<std::string> list(const std::string& dir) const override;
  bool readAll(const std::string& path, std::vector<uint8_t>* out) const override;
  std::string real(const std::string& halPath) const;  // "/hal/x" -> "<root>/x"
  const std::string& root() const { return root_; }

 private:
  std::string root_;
};

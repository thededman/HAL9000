#include "posix_storage.h"

#include <dirent.h>
#include <sys/stat.h>

#include <cstdio>

std::string PosixStorage::real(const std::string& halPath) const {
  const std::string prefix = "/hal";
  if (halPath.compare(0, prefix.size(), prefix) != 0) return root_ + halPath;
  return root_ + halPath.substr(prefix.size());
}

bool PosixStorage::exists(const std::string& path) const {
  struct stat st;
  return stat(real(path).c_str(), &st) == 0;
}

std::vector<std::string> PosixStorage::list(const std::string& dir) const {
  std::vector<std::string> names;
  std::string d = real(dir);
  DIR* h = opendir(d.c_str());
  if (!h) return names;
  while (dirent* e = readdir(h)) {
    std::string n = e->d_name;
    if (n == "." || n == "..") continue;
    struct stat st;
    if (stat((d + "/" + n).c_str(), &st) == 0 && S_ISREG(st.st_mode)) names.push_back(n);
  }
  closedir(h);
  return names;
}

bool PosixStorage::readAll(const std::string& path, std::vector<uint8_t>* out) const {
  FILE* f = std::fopen(real(path).c_str(), "rb");
  if (!f) return false;
  std::fseek(f, 0, SEEK_END);
  long n = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  out->resize(n > 0 ? size_t(n) : 0);
  size_t got = n > 0 ? std::fread(out->data(), 1, out->size(), f) : 0;
  std::fclose(f);
  return got == out->size();
}

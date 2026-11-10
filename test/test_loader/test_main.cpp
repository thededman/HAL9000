#include <unity.h>

#include <map>
#include <string>
#include <vector>

#include "hal_loader.h"

void setUp() {}
void tearDown() {}

namespace {

// An SD card (or ~/hal) in memory: full paths -> contents.
class FakeStorage : public hal::Storage {
 public:
  std::map<std::string, std::string> files;
  bool exists(const std::string& path) const override {
    if (files.count(path)) return true;
    for (auto& kv : files)
      if (kv.first.compare(0, path.size() + 1, path + "/") == 0) return true;  // a directory
    return false;
  }
  std::vector<std::string> list(const std::string& dir) const override {
    std::vector<std::string> names;
    for (auto& kv : files) {
      if (kv.first.compare(0, dir.size() + 1, dir + "/") != 0) continue;
      std::string rest = kv.first.substr(dir.size() + 1);
      if (rest.find('/') == std::string::npos) names.push_back(rest);  // files only
    }
    return names;
  }
  bool readAll(const std::string& path, std::vector<uint8_t>* out) const override {
    auto it = files.find(path);
    if (it == files.end()) return false;
    out->assign(it->second.begin(), it->second.end());
    return true;
  }
};

}  // namespace

void test_config_defaults_when_missing() {
  FakeStorage s;
  std::vector<std::string> w;
  auto c = hal::loadConfig(s, &w);
  TEST_ASSERT_EQUAL(180, c.volume);
  TEST_ASSERT_EQUAL(1, w.size());
  TEST_ASSERT_NOT_EQUAL(std::string::npos, w[0].find("/hal/config.txt"));
}

void test_config_read_from_storage() {
  FakeStorage s;
  s.files["/hal/config.txt"] = "volume=99\ncolour=red\n";
  std::vector<std::string> w;
  auto c = hal::loadConfig(s, &w);
  TEST_ASSERT_EQUAL(99, c.volume);
  TEST_ASSERT_EQUAL(1, w.size());  // the unknown key
}

void test_voice_index_missing_root() {
  FakeStorage s;
  hal::VoiceIndex idx(1);
  TEST_ASSERT_FALSE(hal::loadVoiceIndex(s, idx));
}

void test_voice_index_folders_and_mine() {
  FakeStorage s;
  s.files["/hal/voice/tap/001-a.wav"] = "x";
  s.files["/hal/voice/tap/._001-a.wav"] = "junk";
  s.files["/hal/voice/mine/clip.wav"] = "x";
  s.files["/hal/voice/greet/morning/001-m.wav"] = "x";
  s.files["/hal/voice/plea/001-p.wav"] = "x";
  s.files["/hal/voice/plea/002-p.wav"] = "x";
  hal::VoiceIndex idx(1);
  TEST_ASSERT_TRUE(hal::loadVoiceIndex(s, idx));
  TEST_ASSERT_EQUAL(2, idx.count(hal::Folder::Tap));  // tap + mine, junk ignored
  TEST_ASSERT_EQUAL(1, idx.count(hal::Folder::GreetMorning));
  TEST_ASSERT_EQUAL(2, idx.count(hal::Folder::Plea));
  TEST_ASSERT_EQUAL(0, idx.count(hal::Folder::Idle));
  std::string p;
  TEST_ASSERT_TRUE(idx.at(hal::Folder::Plea, 1, &p));
  TEST_ASSERT_EQUAL_STRING("/hal/voice/plea/002-p.wav", p.c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_config_defaults_when_missing);
  RUN_TEST(test_config_read_from_storage);
  RUN_TEST(test_voice_index_missing_root);
  RUN_TEST(test_voice_index_folders_and_mine);
  return UNITY_END();
}

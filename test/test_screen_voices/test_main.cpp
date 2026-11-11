#include <unity.h>

#include <map>
#include <set>
#include <string>
#include <vector>

#include "hal_screen_voices.h"
#include "hal_storage.h"
#include "hal_voice_index.h"

void setUp() {}
void tearDown() {}

namespace {

class FakeStorage : public hal::Storage {
 public:
  std::map<std::string, std::string> files;
  bool exists(const std::string& path) const override {
    if (files.count(path)) return true;
    for (auto& kv : files)
      if (kv.first.compare(0, path.size() + 1, path + "/") == 0) return true;
    return false;
  }
  std::vector<std::string> list(const std::string& dir) const override {
    std::vector<std::string> names;
    for (auto& kv : files) {
      if (kv.first.compare(0, dir.size() + 1, dir + "/") != 0) continue;
      std::string rest = kv.first.substr(dir.size() + 1);
      if (rest.find('/') == std::string::npos) names.push_back(rest);
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

FakeStorage card() {
  FakeStorage s;
  s.files["/hal/voice/screens/chess/001-a.wav"] = "x";
  s.files["/hal/voice/screens/chess/002-b.wav"] = "x";
  s.files["/hal/voice/screens/chess/notes.txt"] = "x";
  s.files["/hal/voice/screens/memory/._001-a.wav"] = "x";  // macOS leftovers only
  return s;
}

hal::VoiceIndex voices(bool withCamera) {
  hal::VoiceIndex v(7);
  v.setFiles(hal::Folder::Tap, {"t.wav"});
  if (withCamera) v.setFiles(hal::Folder::Camera, {"c.wav"});
  return v;
}

}  // namespace

void test_loads_per_screen() {
  FakeStorage s = card();
  hal::ScreenVoices sv(1);
  sv.load(s, {"chess", "memory"});
  TEST_ASSERT_EQUAL(2, sv.count("chess"));
  TEST_ASSERT_EQUAL(0, sv.count("memory"));
}

void test_pick_no_repeat() {
  FakeStorage s = card();
  hal::ScreenVoices sv(1);
  sv.load(s, {"chess"});
  std::set<std::string> got;
  std::string p;
  for (int i = 0; i < 2; ++i) {
    TEST_ASSERT_TRUE(sv.pick("chess", &p));
    got.insert(p);
  }
  TEST_ASSERT_EQUAL(2, int(got.size()));
  TEST_ASSERT_TRUE(got.count("/hal/voice/screens/chess/001-a.wav"));
  TEST_ASSERT_TRUE(got.count("/hal/voice/screens/chess/002-b.wav"));
}

void test_hidden_files_fall_back() {
  FakeStorage s = card();
  hal::ScreenVoices sv(1);
  sv.load(s, {"memory"});
  TEST_ASSERT_EQUAL(0, sv.count("memory"));
  hal::VoiceIndex v = voices(true);
  std::string p;
  TEST_ASSERT_TRUE(hal::pickConsoleLine("memory", sv, v, &p));
  TEST_ASSERT_EQUAL_STRING("/hal/voice/tap/t.wav", p.c_str());
}

void test_console_line_choice() {
  FakeStorage s = card();
  hal::ScreenVoices sv(1);
  sv.load(s, {"chess", "vitals"});
  hal::VoiceIndex v = voices(true);
  std::string p;
  TEST_ASSERT_TRUE(hal::pickConsoleLine("visual", sv, v, &p));
  TEST_ASSERT_EQUAL_STRING("/hal/voice/camera/c.wav", p.c_str());
  TEST_ASSERT_TRUE(hal::pickConsoleLine("chess", sv, v, &p));
  TEST_ASSERT_EQUAL(0, p.compare(0, 25, "/hal/voice/screens/chess/"));
  TEST_ASSERT_TRUE(hal::pickConsoleLine("vitals", sv, v, &p));
  TEST_ASSERT_EQUAL_STRING("/hal/voice/tap/t.wav", p.c_str());
  hal::VoiceIndex noCam = voices(false);
  TEST_ASSERT_TRUE(hal::pickConsoleLine("visual", sv, noCam, &p));
  TEST_ASSERT_EQUAL_STRING("/hal/voice/tap/t.wav", p.c_str());
}

void test_unknown_id() {
  FakeStorage s = card();
  hal::ScreenVoices sv(1);
  sv.load(s, {"chess"});
  std::string p;
  TEST_ASSERT_EQUAL(0, sv.count("nope"));
  TEST_ASSERT_FALSE(sv.pick("nope", &p));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_loads_per_screen);
  RUN_TEST(test_pick_no_repeat);
  RUN_TEST(test_hidden_files_fall_back);
  RUN_TEST(test_console_line_choice);
  RUN_TEST(test_unknown_id);
  return UNITY_END();
}

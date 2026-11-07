#include <unity.h>

#include <set>
#include <string>

#include "hal_voice_index.h"

using hal::Folder;

void setUp() {}
void tearDown() {}

// Review Focus 3: copying from a Mac leaves ._ files and .DS_Store on the card
void test_junk_files_ignored() {
  hal::VoiceIndex idx(1);
  idx.setFiles(Folder::Tap, {"._001-a.wav", ".DS_Store", ".Trashes", "002-b.wav", "HELLO.WAV", "notes.txt"});
  TEST_ASSERT_EQUAL(2, idx.count(Folder::Tap));
  TEST_ASSERT_TRUE(hal::isVoiceFile("x.Wav"));
  TEST_ASSERT_FALSE(hal::isVoiceFile("._x.wav"));
  TEST_ASSERT_FALSE(hal::isVoiceFile("wav"));
}

void test_paths_and_order() {
  hal::VoiceIndex idx(1);
  idx.setFiles(Folder::Plea, {"003-z.wav", "001-x.wav", "002-y.wav"});
  std::string p;
  TEST_ASSERT_TRUE(idx.at(Folder::Plea, 0, &p));
  TEST_ASSERT_EQUAL_STRING("/hal/voice/plea/001-x.wav", p.c_str());
  TEST_ASSERT_TRUE(idx.at(Folder::Plea, 2, &p));
  TEST_ASSERT_EQUAL_STRING("/hal/voice/plea/003-z.wav", p.c_str());
  TEST_ASSERT_FALSE(idx.at(Folder::Plea, 3, &p));
}

void test_tap_merges_mine() {
  hal::VoiceIndex idx(1);
  idx.setFiles(Folder::Tap, {"001-a.wav", "002-b.wav"});
  idx.addFiles(Folder::Tap, "/hal/voice/mine", {"my-clip.wav"});
  TEST_ASSERT_EQUAL(3, idx.count(Folder::Tap));
  std::set<std::string> seen;
  std::string p;
  for (int i = 0; i < 3; ++i) {
    TEST_ASSERT_TRUE(idx.pick(Folder::Tap, &p));
    seen.insert(p);
  }
  TEST_ASSERT_EQUAL(3, seen.size());
  TEST_ASSERT_EQUAL(1, seen.count("/hal/voice/mine/my-clip.wav"));
}

void test_empty_folder_pick_false() {
  hal::VoiceIndex idx(1);
  std::string p = "unchanged";
  TEST_ASSERT_FALSE(idx.pick(Folder::Idle, &p));
  TEST_ASSERT_EQUAL_STRING("unchanged", p.c_str());
  TEST_ASSERT_EQUAL(0, idx.count(Folder::Idle));
}

void test_folder_paths() {
  TEST_ASSERT_EQUAL_STRING("/hal/voice/greet/evening", hal::folderPath(Folder::GreetEvening));
  TEST_ASSERT_EQUAL_STRING("/hal/voice/greet/morning", hal::folderPath(Folder::GreetMorning));
  TEST_ASSERT_EQUAL_STRING("/hal/voice/boot", hal::folderPath(Folder::Boot));
  TEST_ASSERT_EQUAL_STRING("/hal/voice/daisy", hal::folderPath(Folder::Daisy));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_junk_files_ignored);
  RUN_TEST(test_paths_and_order);
  RUN_TEST(test_tap_merges_mine);
  RUN_TEST(test_empty_folder_pick_false);
  RUN_TEST(test_folder_paths);
  return UNITY_END();
}

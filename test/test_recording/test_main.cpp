#include <unity.h>

#include <cmath>
#include <cstdlib>
#include <vector>

#include "hal_recording.h"
#include "hal_wav.h"

using hal::Recording;

void setUp() {}
void tearDown() {}

namespace {

std::vector<int16_t> tone(double seconds, int amplitude) {
  std::vector<int16_t> s(static_cast<size_t>(seconds * Recording::kRate));
  for (size_t i = 0; i < s.size(); ++i) s[i] = static_cast<int16_t>(amplitude * std::sin(i * 2 * 3.14159265 * 440 / Recording::kRate));
  return s;
}

std::vector<int16_t> silence(double seconds) { return std::vector<int16_t>(static_cast<size_t>(seconds * Recording::kRate), 0); }

void add(Recording& r, const std::vector<int16_t>& s) { r.append(s.data(), s.size()); }

int peakOf(const std::vector<uint8_t>& wav) {
  hal::WavInfo info;
  TEST_ASSERT_TRUE(hal::parseWav(wav.data(), wav.size(), &info));
  const auto* pcm = reinterpret_cast<const int16_t*>(wav.data() + info.dataOffset);
  int peak = 0;
  for (size_t i = 0; i < info.dataBytes / 2; ++i) peak = std::max(peak, std::abs(int(pcm[i])));
  return peak;
}

uint32_t msOf(const std::vector<uint8_t>& wav) {
  hal::WavInfo info;
  TEST_ASSERT_TRUE(hal::parseWav(wav.data(), wav.size(), &info));
  return info.durationMs;
}

}  // namespace

void test_wav_round_trip() {
  Recording r;
  r.start();
  add(r, tone(1.0, 9830));  // 30 % of full scale
  TEST_ASSERT_EQUAL(1000, r.ms());
  std::vector<uint8_t> wav = r.wav();
  hal::WavInfo info;
  TEST_ASSERT_TRUE(hal::parseWav(wav.data(), wav.size(), &info));
  TEST_ASSERT_EQUAL(16000, info.sampleRate);
  TEST_ASSERT_EQUAL(1, info.channels);
  TEST_ASSERT_EQUAL(16, info.bitsPerSample);
  TEST_ASSERT_INT_WITHIN(60, 1000, int(info.durationMs));
}

void test_trims_silence() {
  Recording r;
  r.start();
  add(r, silence(1.0));
  add(r, tone(1.0, 9000));
  add(r, silence(1.0));
  uint32_t ms = msOf(r.wav());
  TEST_ASSERT_TRUE(ms >= 1000 && ms <= 1500);  // the tone, with a little room either side

  Recording noisy;  // room noise before and after doesn't count as speech
  noisy.start();
  std::vector<int16_t> hiss(Recording::kRate);
  for (size_t i = 0; i < hiss.size(); ++i) hiss[i] = static_cast<int16_t>((i * 7919) % 41 - 20);
  add(noisy, hiss);
  add(noisy, tone(1.0, 3000));
  add(noisy, hiss);
  ms = msOf(noisy.wav());
  TEST_ASSERT_TRUE(ms >= 1000 && ms <= 1500);
}

void test_quiet_is_brought_up() {
  Recording r;
  r.start();
  add(r, tone(1.0, 230));  // about what speech from across the room measured on the Cardputer
  int peak = peakOf(r.wav());
  TEST_ASSERT_TRUE(peak >= 8000 && peak <= 16500);
}

void test_gain_is_limited() {
  Recording r;
  r.start();
  add(r, tone(1.0, 160));
  int peak = peakOf(r.wav());
  TEST_ASSERT_TRUE(peak <= 160 * 40 + 40);  // never more than 40 times louder
  TEST_ASSERT_TRUE(peak >= 160 * 30);
}

void test_loud_is_left_alone() {
  Recording r;
  r.start();
  add(r, tone(1.0, 19660));  // 60 %
  TEST_ASSERT_INT_WITHIN(2, 19660, peakOf(r.wav()));
}

void test_gain_never_clips() {
  Recording r;
  r.start();
  std::vector<int16_t> s = tone(1.0, 400);
  s[4000] = 3000;  // a click: the gain follows the loudest sample, so nothing can pass full scale
  s[9000] = -3000;
  add(r, s);
  std::vector<uint8_t> wav = r.wav();
  int peak = peakOf(wav);
  TEST_ASSERT_TRUE(peak <= 16500);
  TEST_ASSERT_TRUE(peak >= 16000);
}

void test_too_short() {
  Recording r;
  r.start();
  add(r, tone(0.2, 9000));  // a brushed key
  TEST_ASSERT_EQUAL(0, int(r.wav().size()));
  Recording blip;  // or a long hold with only a click in it
  blip.start();
  add(blip, silence(1.0));
  add(blip, tone(0.05, 9000));
  add(blip, silence(1.0));
  TEST_ASSERT_EQUAL(0, int(blip.wav().size()));
}

void test_too_quiet() {
  Recording r;
  r.start();
  std::vector<int16_t> hiss(2 * Recording::kRate);
  for (size_t i = 0; i < hiss.size(); ++i) hiss[i] = static_cast<int16_t>((i * 7919) % 121 - 60);  // peaks at 60
  add(r, hiss);
  TEST_ASSERT_EQUAL(0, int(r.wav().size()));
  Recording nothing;
  nothing.start();
  TEST_ASSERT_EQUAL(0, int(nothing.wav().size()));
  add(nothing, silence(3.0));
  TEST_ASSERT_EQUAL(0, int(nothing.wav().size()));
}

void test_cap() {
  Recording r;
  r.start();
  for (int i = 0; i < 20; ++i) add(r, tone(1.0, 9000));
  TEST_ASSERT_TRUE(r.full());
  TEST_ASSERT_EQUAL(15000, r.ms());
  TEST_ASSERT_TRUE(msOf(r.wav()) <= 15000);
  TEST_ASSERT_TRUE(r.wav().size() <= 44 + Recording::kMaxSamples * 2);
}

void test_level() {
  Recording r;
  r.start();
  TEST_ASSERT_EQUAL(0, r.level());
  add(r, silence(0.5));
  TEST_ASSERT_EQUAL(0, r.level());
  add(r, tone(0.5, 9830));
  TEST_ASSERT_TRUE(r.level() > 150);
  add(r, tone(0.5, 230));  // quiet speech still shows
  TEST_ASSERT_TRUE(r.level() > 30 && r.level() < 150);
  add(r, silence(0.5));
  TEST_ASSERT_EQUAL(0, r.level());
}

void test_start_forgets() {
  Recording r;
  r.start();
  add(r, tone(1.0, 9000));
  r.start();
  TEST_ASSERT_EQUAL(0, r.ms());
  TEST_ASSERT_FALSE(r.full());
  TEST_ASSERT_EQUAL(0, int(r.wav().size()));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_wav_round_trip);
  RUN_TEST(test_trims_silence);
  RUN_TEST(test_quiet_is_brought_up);
  RUN_TEST(test_gain_is_limited);
  RUN_TEST(test_loud_is_left_alone);
  RUN_TEST(test_gain_never_clips);
  RUN_TEST(test_too_short);
  RUN_TEST(test_too_quiet);
  RUN_TEST(test_cap);
  RUN_TEST(test_level);
  RUN_TEST(test_start_forgets);
  return UNITY_END();
}

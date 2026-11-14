#include <unity.h>

#include <cstdint>
#include <string>
#include <vector>

#include "hal_wav.h"

void setUp() {}
void tearDown() {}

namespace {

struct Chunk {
  std::string id;
  std::vector<uint8_t> body;
};

void put16(std::vector<uint8_t>& v, uint16_t x) {
  v.push_back(x & 0xFF);
  v.push_back(x >> 8);
}
void put32(std::vector<uint8_t>& v, uint32_t x) {
  for (int i = 0; i < 4; ++i) v.push_back((x >> (8 * i)) & 0xFF);
}
void putId(std::vector<uint8_t>& v, const std::string& id) { v.insert(v.end(), id.begin(), id.end()); }

// Builds a RIFF/WAVE file. `dataSizeOverride` lets a test lie about the data size.
std::vector<uint8_t> makeWav(uint32_t rate, uint16_t channels, uint16_t bits,
                             const std::vector<int16_t>& samples,
                             const std::vector<Chunk>& extra = {}, uint16_t format = 1,
                             uint32_t dataSizeOverride = 0) {
  std::vector<uint8_t> body;
  putId(body, "WAVE");
  putId(body, "fmt ");
  put32(body, 16);
  put16(body, format);
  put16(body, channels);
  put32(body, rate);
  put32(body, rate * channels * bits / 8);
  put16(body, channels * bits / 8);
  put16(body, bits);
  for (const Chunk& c : extra) {
    putId(body, c.id);
    put32(body, c.body.size());
    body.insert(body.end(), c.body.begin(), c.body.end());
    if (c.body.size() % 2) body.push_back(0);  // pad byte
  }
  putId(body, "data");
  put32(body, dataSizeOverride ? dataSizeOverride : samples.size() * 2);
  for (int16_t s : samples) put16(body, static_cast<uint16_t>(s));
  std::vector<uint8_t> out;
  putId(out, "RIFF");
  put32(out, body.size());
  out.insert(out.end(), body.begin(), body.end());
  return out;
}

}  // namespace

void test_parse_plain_mono() {
  auto f = makeWav(22050, 1, 16, std::vector<int16_t>(2205, 0));
  hal::WavInfo info;
  TEST_ASSERT_TRUE(hal::parseWav(f.data(), f.size(), &info));
  TEST_ASSERT_EQUAL_UINT32(22050, info.sampleRate);
  TEST_ASSERT_EQUAL(1, info.channels);
  TEST_ASSERT_EQUAL(16, info.bitsPerSample);
  TEST_ASSERT_EQUAL(44, info.dataOffset);
  TEST_ASSERT_EQUAL(4410, info.dataBytes);
  TEST_ASSERT_EQUAL_UINT32(100, info.durationMs);
}

// Review Focus 4: ffmpeg writes a LIST chunk before data
void test_parse_with_list_chunk() {
  Chunk list{"LIST", std::vector<uint8_t>(26, 'x')};
  auto f = makeWav(22050, 1, 16, std::vector<int16_t>(100, 0), {list});
  hal::WavInfo info;
  TEST_ASSERT_TRUE(hal::parseWav(f.data(), f.size(), &info));
  TEST_ASSERT_EQUAL(44 + 8 + 26, info.dataOffset);
  TEST_ASSERT_EQUAL(200, info.dataBytes);
}

// Review Focus 4: odd-sized chunks are followed by a pad byte
void test_parse_odd_chunk_padding() {
  Chunk junk{"junk", {1, 2, 3}};
  auto f = makeWav(22050, 1, 16, std::vector<int16_t>(10, 0), {junk});
  hal::WavInfo info;
  TEST_ASSERT_TRUE(hal::parseWav(f.data(), f.size(), &info));
  TEST_ASSERT_EQUAL(44 + 8 + 4, info.dataOffset);
  TEST_ASSERT_EQUAL(20, info.dataBytes);
}

// Review Focus 4: a cut-off file never reads past the buffer
void test_truncated_data_clamped() {
  auto f = makeWav(22050, 1, 16, std::vector<int16_t>(200, 0), {}, 1, 10000);
  hal::WavInfo info;
  TEST_ASSERT_TRUE(hal::parseWav(f.data(), f.size(), &info));
  TEST_ASSERT_EQUAL(400, info.dataBytes);
  TEST_ASSERT_EQUAL(f.size(), info.dataOffset + info.dataBytes);
}

void test_rejects_non_pcm_and_8bit() {
  hal::WavInfo info;
  auto flt = makeWav(22050, 1, 16, std::vector<int16_t>(10, 0), {}, 3);
  TEST_ASSERT_FALSE(hal::parseWav(flt.data(), flt.size(), &info));
  auto eight = makeWav(22050, 1, 8, std::vector<int16_t>(10, 0));
  TEST_ASSERT_FALSE(hal::parseWav(eight.data(), eight.size(), &info));
}

void test_rejects_garbage() {
  hal::WavInfo info;
  const uint8_t junk[12] = {7, 1, 9, 3, 4, 4, 0, 255, 8, 2, 6, 5};
  TEST_ASSERT_FALSE(hal::parseWav(junk, sizeof junk, &info));
  TEST_ASSERT_FALSE(hal::parseWav(junk, 0, &info));
  auto f = makeWav(22050, 1, 16, std::vector<int16_t>(10, 0));
  TEST_ASSERT_FALSE(hal::parseWav(f.data(), 30, &info));  // cut inside fmt
}

void test_envelope_silence_and_full() {
  auto quiet = makeWav(22050, 1, 16, std::vector<int16_t>(2205, 0));
  hal::WavInfo info;
  TEST_ASSERT_TRUE(hal::parseWav(quiet.data(), quiet.size(), &info));
  auto env = hal::loudnessEnvelope(quiet.data(), info, 20);
  TEST_ASSERT_EQUAL(5, env.size());  // 2205 samples / 441 per 20 ms frame
  for (uint8_t v : env) TEST_ASSERT_EQUAL(0, v);

  std::vector<int16_t> square(2205);
  for (size_t i = 0; i < square.size(); ++i) square[i] = (i % 2) ? 32767 : -32767;
  auto loud = makeWav(22050, 1, 16, square);
  TEST_ASSERT_TRUE(hal::parseWav(loud.data(), loud.size(), &info));
  env = hal::loudnessEnvelope(loud.data(), info, 20);
  TEST_ASSERT_EQUAL(5, env.size());
  for (uint8_t v : env) TEST_ASSERT_EQUAL(255, v);
}

// An unsigned 8-bit mono WAV, as the brain renders HAL Card's lines (his device has little memory).
static std::vector<uint8_t> makeWav8(uint32_t rate, const std::vector<uint8_t>& samples) {
  std::vector<uint8_t> body;
  putId(body, "WAVE");
  putId(body, "fmt ");
  put32(body, 16);
  put16(body, 1);
  put16(body, 1);
  put32(body, rate);
  put32(body, rate);
  put16(body, 1);
  put16(body, 8);
  putId(body, "data");
  put32(body, samples.size());
  body.insert(body.end(), samples.begin(), samples.end());
  std::vector<uint8_t> out;
  putId(out, "RIFF");
  put32(out, body.size());
  out.insert(out.end(), body.begin(), body.end());
  return out;
}

void test_clip_may_be_8_bit() {
  auto eight = makeWav8(11025, std::vector<uint8_t>(11025, 128));
  hal::WavInfo info;
  TEST_ASSERT_TRUE(hal::parseClipWav(eight.data(), eight.size(), &info));
  TEST_ASSERT_EQUAL(8, info.bitsPerSample);
  TEST_ASSERT_EQUAL(11025, info.sampleRate);
  TEST_ASSERT_EQUAL(11025, int(info.dataBytes));
  TEST_ASSERT_EQUAL(1000, int(info.durationMs));
  TEST_ASSERT_TRUE(hal::playableClip(info));
  TEST_ASSERT_FALSE(hal::parseWav(eight.data(), eight.size(), &info));  // the voice files on the card stay 16-bit
  // ... and 16-bit clips are clips too, read exactly as before.
  auto sixteen = makeWav(22050, 1, 16, std::vector<int16_t>(22050, 0));
  hal::WavInfo a, b;
  TEST_ASSERT_TRUE(hal::parseClipWav(sixteen.data(), sixteen.size(), &a));
  TEST_ASSERT_TRUE(hal::parseWav(sixteen.data(), sixteen.size(), &b));
  TEST_ASSERT_EQUAL(int(b.durationMs), int(a.durationMs));
  TEST_ASSERT_EQUAL(16, a.bitsPerSample);
  // Nothing else gets in: 24-bit, float, garbage.
  auto wide = makeWav(22050, 1, 24, std::vector<int16_t>(10, 0));
  TEST_ASSERT_FALSE(hal::parseClipWav(wide.data(), wide.size(), &a));
  auto flt = makeWav(22050, 1, 16, std::vector<int16_t>(10, 0), {}, 3);
  TEST_ASSERT_FALSE(hal::parseClipWav(flt.data(), flt.size(), &a));
  TEST_ASSERT_FALSE(hal::parseClipWav(nullptr, 0, &a));
}

void test_envelope_of_an_8_bit_clip() {
  // Silence in unsigned 8-bit is 128, not 0; full scale swings between 0 and 255.
  auto quiet = makeWav8(11025, std::vector<uint8_t>(2205, 128));
  hal::WavInfo info;
  TEST_ASSERT_TRUE(hal::parseClipWav(quiet.data(), quiet.size(), &info));
  auto env = hal::loudnessEnvelope(quiet.data(), info, 20);
  TEST_ASSERT_EQUAL(11, int(env.size()));  // 2205 samples / 220 per 20 ms frame
  for (uint8_t v : env) TEST_ASSERT_EQUAL(0, v);
  std::vector<uint8_t> square(2205);
  for (size_t i = 0; i < square.size(); ++i) square[i] = (i % 2) ? 255 : 1;
  auto loud = makeWav8(11025, square);
  TEST_ASSERT_TRUE(hal::parseClipWav(loud.data(), loud.size(), &info));
  env = hal::loudnessEnvelope(loud.data(), info, 20);
  for (uint8_t v : env) TEST_ASSERT_TRUE(v >= 250);
}

void test_playable_clip() {
  hal::WavInfo ok;
  ok.sampleRate = 22050;
  ok.channels = 1;
  ok.bitsPerSample = 16;
  ok.dataBytes = 44100;
  ok.durationMs = 1000;
  TEST_ASSERT_TRUE(hal::playableClip(ok));
  hal::WavInfo w = ok;
  w.channels = 2;
  TEST_ASSERT_TRUE(hal::playableClip(w));
  w.channels = 6;  // would play as noise
  TEST_ASSERT_FALSE(hal::playableClip(w));
  w = ok;
  w.sampleRate = 50;  // a megabyte at 50 Hz would "speak" for hours
  TEST_ASSERT_FALSE(hal::playableClip(w));
  w.sampleRate = 96000;
  TEST_ASSERT_FALSE(hal::playableClip(w));
  w = ok;
  w.dataBytes = 0;
  TEST_ASSERT_FALSE(hal::playableClip(w));
  w = ok;
  w.durationMs = 60001;  // no line is a minute long
  TEST_ASSERT_FALSE(hal::playableClip(w));
  w.durationMs = 60000;
  TEST_ASSERT_TRUE(hal::playableClip(w));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_parse_plain_mono);
  RUN_TEST(test_parse_with_list_chunk);
  RUN_TEST(test_parse_odd_chunk_padding);
  RUN_TEST(test_truncated_data_clamped);
  RUN_TEST(test_rejects_non_pcm_and_8bit);
  RUN_TEST(test_rejects_garbage);
  RUN_TEST(test_envelope_silence_and_full);
  RUN_TEST(test_playable_clip);
  RUN_TEST(test_clip_may_be_8_bit);
  RUN_TEST(test_envelope_of_an_8_bit_clip);
  return UNITY_END();
}

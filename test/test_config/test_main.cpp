#include <unity.h>
#include "hal_config.h"

static std::vector<std::string> w;

void setUp() { w.clear(); }
void tearDown() {}

void test_defaults_on_empty() {
  auto c = hal::parseConfig("", &w);
  TEST_ASSERT_EQUAL_STRING("", c.wifiSsid.c_str());
  TEST_ASSERT_EQUAL_STRING("", c.wifiPassword.c_str());
  TEST_ASSERT_EQUAL_STRING("EST5EDT,M3.2.0,M11.1.0", c.timezone.c_str());
  TEST_ASSERT_EQUAL(22 * 60, c.quietStartMin);
  TEST_ASSERT_EQUAL(7 * 60 + 30, c.quietEndMin);
  TEST_ASSERT_EQUAL(180, c.volume);
  TEST_ASSERT_EQUAL(90, c.quietVolume);
  TEST_ASSERT_EQUAL(20, c.idleMinMinutes);
  TEST_ASSERT_EQUAL(60, c.idleMaxMinutes);
  TEST_ASSERT_EQUAL(10, c.greetCooldownMinutes);
  TEST_ASSERT_EQUAL(20, c.shakeCooldownSeconds);
  TEST_ASSERT_EQUAL(120, c.proximityThreshold);
  TEST_ASSERT_EQUAL(3, c.holdSeconds);
  TEST_ASSERT_EQUAL(0, w.size());
}

void test_full_file() {
  auto c = hal::parseConfig(
      "# HAL settings\n"
      "wifi_ssid=Discovery\n"
      "wifi_password=jupiter\n"
      "timezone=PST8PDT,M3.2.0,M11.1.0\n"
      "quiet_start=23:00\n"
      "quiet_end=06:45\n"
      "volume=200\n"
      "quiet_volume=40\n"
      "idle_min_minutes=5\n"
      "idle_max_minutes=15\n"
      "greet_cooldown_minutes=30\n"
      "shake_cooldown_seconds=0\n"
      "proximity_threshold=500\n"
      "hold_seconds=5\n",
      &w);
  TEST_ASSERT_EQUAL_STRING("Discovery", c.wifiSsid.c_str());
  TEST_ASSERT_EQUAL_STRING("jupiter", c.wifiPassword.c_str());
  TEST_ASSERT_EQUAL_STRING("PST8PDT,M3.2.0,M11.1.0", c.timezone.c_str());
  TEST_ASSERT_EQUAL(23 * 60, c.quietStartMin);
  TEST_ASSERT_EQUAL(6 * 60 + 45, c.quietEndMin);
  TEST_ASSERT_EQUAL(200, c.volume);
  TEST_ASSERT_EQUAL(40, c.quietVolume);
  TEST_ASSERT_EQUAL(5, c.idleMinMinutes);
  TEST_ASSERT_EQUAL(15, c.idleMaxMinutes);
  TEST_ASSERT_EQUAL(30, c.greetCooldownMinutes);
  TEST_ASSERT_EQUAL(0, c.shakeCooldownSeconds);
  TEST_ASSERT_EQUAL(500, c.proximityThreshold);
  TEST_ASSERT_EQUAL(5, c.holdSeconds);
  TEST_ASSERT_EQUAL(0, w.size());
}

// Review Focus 1: TextEdit adds a BOM and may use CRLF
void test_bom_and_crlf() {
  auto c = hal::parseConfig("\xEF\xBB\xBFvolume=200\r\nquiet_start=21:15\r\n", &w);
  TEST_ASSERT_EQUAL(200, c.volume);
  TEST_ASSERT_EQUAL(21 * 60 + 15, c.quietStartMin);
  TEST_ASSERT_EQUAL(0, w.size());
}

// Review Focus 2: passwords with #, = and spaces are taken verbatim
void test_password_verbatim() {
  auto c = hal::parseConfig("wifi_password=  p#ss = word 1  \nwifi_ssid=My Home #5\n", &w);
  TEST_ASSERT_EQUAL_STRING("p#ss = word 1", c.wifiPassword.c_str());
  TEST_ASSERT_EQUAL_STRING("My Home #5", c.wifiSsid.c_str());
  TEST_ASSERT_EQUAL(0, w.size());
}

void test_inline_comment_on_other_keys() {
  auto c = hal::parseConfig("volume=150   # loud-ish\n  quiet_end = 08:00\t# weekends\n", &w);
  TEST_ASSERT_EQUAL(150, c.volume);
  TEST_ASSERT_EQUAL(8 * 60, c.quietEndMin);
  TEST_ASSERT_EQUAL(0, w.size());
}

void test_bad_values_warn_and_default() {
  auto c = hal::parseConfig("volume=loud\nquiet_volume=300\nquiet_end=25:00\nhold_seconds=0\n", &w);
  TEST_ASSERT_EQUAL(180, c.volume);
  TEST_ASSERT_EQUAL(90, c.quietVolume);
  TEST_ASSERT_EQUAL(7 * 60 + 30, c.quietEndMin);
  TEST_ASSERT_EQUAL(3, c.holdSeconds);
  TEST_ASSERT_EQUAL(4, w.size());
  TEST_ASSERT_NOT_EQUAL(std::string::npos, w[0].find("volume"));
}

void test_unknown_key_warns() {
  auto c = hal::parseConfig("colour=red\n", &w);
  TEST_ASSERT_EQUAL(1, w.size());
  TEST_ASSERT_NOT_EQUAL(std::string::npos, w[0].find("colour"));
  TEST_ASSERT_EQUAL(180, c.volume);
}

void test_idle_min_over_max_resets() {
  auto c = hal::parseConfig("idle_min_minutes=90\nidle_max_minutes=30\n", &w);
  TEST_ASSERT_EQUAL(20, c.idleMinMinutes);
  TEST_ASSERT_EQUAL(60, c.idleMaxMinutes);
  TEST_ASSERT_EQUAL(1, w.size());
}

// The ESP32 clock wants a POSIX TZ string; a city name silently means UTC.
void test_bad_timezone_warns_and_defaults() {
  auto c = hal::parseConfig("timezone=America/New_York\n", &w);
  TEST_ASSERT_EQUAL_STRING("EST5EDT,M3.2.0,M11.1.0", c.timezone.c_str());
  TEST_ASSERT_EQUAL(1, w.size());
  TEST_ASSERT_NOT_EQUAL(std::string::npos, w[0].find("timezone"));
  w.clear();
  c = hal::parseConfig("timezone=UTC\n", &w);
  TEST_ASSERT_EQUAL_STRING("EST5EDT,M3.2.0,M11.1.0", c.timezone.c_str());
  TEST_ASSERT_EQUAL(1, w.size());
  w.clear();
  c = hal::parseConfig("timezone=UTC0\n", &w);
  TEST_ASSERT_EQUAL_STRING("UTC0", c.timezone.c_str());
  TEST_ASSERT_EQUAL(0, w.size());
}

void test_camera_defaults() {
  auto c = hal::parseConfig("", &w);
  TEST_ASSERT_TRUE(c.camera);
  TEST_ASSERT_EQUAL(20, c.cameraMotionThreshold);
  TEST_ASSERT_EQUAL(0, w.size());
}

// Review Focus 3: booleans in any case
void test_camera_booleans() {
  auto c = hal::parseConfig("camera=Off\n", &w);
  TEST_ASSERT_FALSE(c.camera);
  c = hal::parseConfig("camera=YES\ncamera_motion_threshold=35\n", &w);
  TEST_ASSERT_TRUE(c.camera);
  TEST_ASSERT_EQUAL(35, c.cameraMotionThreshold);
  TEST_ASSERT_EQUAL(0, w.size());
}

void test_camera_bad_values() {
  auto c = hal::parseConfig("camera=maybe\ncamera_motion_threshold=0\n", &w);
  TEST_ASSERT_TRUE(c.camera);
  TEST_ASSERT_EQUAL(20, c.cameraMotionThreshold);
  TEST_ASSERT_EQUAL(2, w.size());
  w.clear();
  c = hal::parseConfig("camera_motion_threshold=300\n", &w);
  TEST_ASSERT_EQUAL(20, c.cameraMotionThreshold);
  TEST_ASSERT_EQUAL(1, w.size());
}

// The preview was removed in 0.3; cards written for 0.2 still say camera_preview=...
void test_retired_camera_preview_is_ignored() {
  auto c = hal::parseConfig("camera_preview=on\n", &w);
  TEST_ASSERT_EQUAL(0, w.size());
  TEST_ASSERT_TRUE(c.camera);
}

// Linking the two HALs
void test_link_defaults() {
  auto c = hal::parseConfig("", &w);
  TEST_ASSERT_FALSE(c.link);
  TEST_ASSERT_EQUAL(9000, c.linkPort);
  TEST_ASSERT_EQUAL_STRING("hal9000-old.local", c.peerHost.c_str());
}

void test_link_settings() {
  auto c = hal::parseConfig("link=on\nlink_port=9100\npeer_host=192.168.1.40\n", &w);
  TEST_ASSERT_TRUE(c.link);
  TEST_ASSERT_EQUAL(9100, c.linkPort);
  TEST_ASSERT_EQUAL_STRING("192.168.1.40", c.peerHost.c_str());
  TEST_ASSERT_EQUAL(0, w.size());
}

void test_link_bad_values() {
  auto c = hal::parseConfig("link=sometimes\nlink_port=80\npeer_host=two words\n", &w);
  TEST_ASSERT_FALSE(c.link);
  TEST_ASSERT_EQUAL(9000, c.linkPort);
  TEST_ASSERT_EQUAL_STRING("hal9000-old.local", c.peerHost.c_str());
  TEST_ASSERT_EQUAL(3, w.size());
}

// ---- the AI brain ----

void test_chat_defaults() {
  auto c = hal::parseConfig("", &w);
  TEST_ASSERT_TRUE(c.chat);
  TEST_ASSERT_EQUAL(20, c.chatMinMinutes);
  TEST_ASSERT_EQUAL(40, c.chatMaxMinutes);
  TEST_ASSERT_EQUAL(0, w.size());
}

void test_chat_settings() {
  auto c = hal::parseConfig("chat=off\nchat_min_minutes=5\nchat_max_minutes=90\n", &w);
  TEST_ASSERT_FALSE(c.chat);
  TEST_ASSERT_EQUAL(5, c.chatMinMinutes);
  TEST_ASSERT_EQUAL(90, c.chatMaxMinutes);
  TEST_ASSERT_EQUAL(0, w.size());
}

void test_chat_bad_values_warn() {
  auto c = hal::parseConfig("chat=sometimes\nchat_min_minutes=0\nchat_max_minutes=99999\n", &w);
  TEST_ASSERT_TRUE(c.chat);
  TEST_ASSERT_EQUAL(20, c.chatMinMinutes);
  TEST_ASSERT_EQUAL(40, c.chatMaxMinutes);
  TEST_ASSERT_EQUAL(3, w.size());
  w.clear();
  // A minimum above the maximum means "at least this long": the maximum comes up to meet it.
  // (Falling back to 20 and 40 would undo what the owner asked for: fewer chats.)
  c = hal::parseConfig("chat_min_minutes=60\nchat_max_minutes=30\n", &w);
  TEST_ASSERT_EQUAL(60, c.chatMinMinutes);
  TEST_ASSERT_EQUAL(60, c.chatMaxMinutes);
  TEST_ASSERT_EQUAL(1, w.size());
  w.clear();
  c = hal::parseConfig("chat_min_minutes=120\n", &w);  // only the minimum given, above the usual maximum
  TEST_ASSERT_EQUAL(120, c.chatMinMinutes);
  TEST_ASSERT_EQUAL(120, c.chatMaxMinutes);
}

void test_brain_defaults() {
  auto c = hal::parseConfig("", &w);
  TEST_ASSERT_FALSE(c.brain);
  TEST_ASSERT_EQUAL_STRING("", c.brainHost.c_str());
  TEST_ASSERT_EQUAL(9100, c.brainPort);
  TEST_ASSERT_EQUAL_STRING("", c.brainKey.c_str());
  TEST_ASSERT_FALSE(hal::brainReady(c));
}

void test_brain_settings() {
  auto c = hal::parseConfig("brain=on\nbrain_host=my-computer.local\nbrain_port=9200\n"
                            "brain_key=0123456789abcdef0123456789abcdef   # written by brain.sh pair\n", &w);
  TEST_ASSERT_TRUE(c.brain);
  TEST_ASSERT_EQUAL_STRING("my-computer.local", c.brainHost.c_str());
  TEST_ASSERT_EQUAL(9200, c.brainPort);
  TEST_ASSERT_EQUAL_STRING("0123456789abcdef0123456789abcdef", c.brainKey.c_str());
  TEST_ASSERT_EQUAL(0, w.size());
  TEST_ASSERT_TRUE(hal::brainReady(c));
  TEST_ASSERT_EQUAL_STRING("192.168.1.20", hal::parseConfig("brain_host=192.168.1.20\n", &w).brainHost.c_str());
}

void test_brain_bad_values_warn() {
  auto c = hal::parseConfig("brain=maybe\nbrain_host=two words\nbrain_port=80\n"
                            "brain_key=0123456789ABCDEF0123456789abcdef\n", &w);
  TEST_ASSERT_FALSE(c.brain);
  TEST_ASSERT_EQUAL_STRING("", c.brainHost.c_str());
  TEST_ASSERT_EQUAL(9100, c.brainPort);
  TEST_ASSERT_EQUAL_STRING("", c.brainKey.c_str());
  TEST_ASSERT_EQUAL(4, w.size());
  for (const std::string& warning : w)  // a key is never repeated in a warning, even a wrong one
    TEST_ASSERT_TRUE(warning.find("0123456789ABCDEF") == std::string::npos);
  w.clear();
  for (const char* key : {"0123456789abcdef0123456789abcde", "0123456789abcdef0123456789abcdef0", "", "not-a-key"}) {
    auto k = hal::parseConfig(std::string("brain_key=") + key + "\n", &w);
    TEST_ASSERT_EQUAL_STRING("", k.brainKey.c_str());
  }
  TEST_ASSERT_EQUAL(4, w.size());
}

void test_brain_ready() {
  const std::string key = "brain_key=0123456789abcdef0123456789abcdef\n";
  TEST_ASSERT_FALSE(hal::brainReady(hal::parseConfig("brain=on\nbrain_host=mac.local\n", &w)));           // no key
  TEST_ASSERT_FALSE(hal::brainReady(hal::parseConfig("brain=on\n" + key, &w)));                            // no host
  TEST_ASSERT_FALSE(hal::brainReady(hal::parseConfig("brain=off\nbrain_host=mac.local\n" + key, &w)));    // switched off
  TEST_ASSERT_TRUE(hal::brainReady(hal::parseConfig("brain=on\nbrain_host=mac.local\n" + key, &w)));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_defaults_on_empty);
  RUN_TEST(test_full_file);
  RUN_TEST(test_bom_and_crlf);
  RUN_TEST(test_password_verbatim);
  RUN_TEST(test_inline_comment_on_other_keys);
  RUN_TEST(test_bad_values_warn_and_default);
  RUN_TEST(test_unknown_key_warns);
  RUN_TEST(test_idle_min_over_max_resets);
  RUN_TEST(test_bad_timezone_warns_and_defaults);
  RUN_TEST(test_camera_defaults);
  RUN_TEST(test_camera_booleans);
  RUN_TEST(test_camera_bad_values);
  RUN_TEST(test_retired_camera_preview_is_ignored);
  RUN_TEST(test_link_defaults);
  RUN_TEST(test_link_settings);
  RUN_TEST(test_link_bad_values);
  RUN_TEST(test_chat_defaults);
  RUN_TEST(test_chat_settings);
  RUN_TEST(test_chat_bad_values_warn);
  RUN_TEST(test_brain_defaults);
  RUN_TEST(test_brain_settings);
  RUN_TEST(test_brain_bad_values_warn);
  RUN_TEST(test_brain_ready);
  return UNITY_END();
}

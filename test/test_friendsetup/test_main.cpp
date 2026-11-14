#include <unity.h>

#include <string>

#include "hal_friendsetup.h"

void setUp() {}
void tearDown() {}

static std::string hex(const std::string& text) {
  static const char digits[] = "0123456789abcdef";
  std::string out;
  for (unsigned char c : text) {
    out.push_back(digits[c >> 4]);
    out.push_back(digits[c & 15]);
  }
  return out;
}

void test_a_setup_line_carries_the_settings() {
  const std::string text = "wifi_ssid=Discovery One\nwifi_password=open the door 2001\n";
  const std::string line = hal::formatFriendSetup(text);
  TEST_ASSERT_EQUAL_STRING(("HALCFG " + hex(text)).c_str(), line.c_str());
  hal::FriendSetup got = hal::parseFriendSetup(line);
  TEST_ASSERT_TRUE(got.mine);
  TEST_ASSERT_TRUE(got.ok);
  TEST_ASSERT_EQUAL_STRING(text.c_str(), got.text.c_str());
  got = hal::parseFriendSetup(line + "\r");  // a line ending from elsewhere
  TEST_ASSERT_TRUE(got.ok);
  std::string upper = line;
  for (char& c : upper)
    if (c >= 'a' && c <= 'f') c = char(c - 32);
  upper.replace(0, 6, "HALCFG");
  TEST_ASSERT_TRUE(hal::parseFriendSetup(upper).ok);  // either case of hex
}

void test_the_password_may_hold_anything_printable() {
  // Spaces, '#', '=' and the rest: only the ends of a value are trimmed by the settings reader.
  const std::string text = "wifi_ssid=Net #5 = home\nwifi_password=a#b=c d!\"$%&'()*+,-./:;<>?@[]^_`{|}~\n";
  hal::FriendSetup got = hal::parseFriendSetup(hal::formatFriendSetup(text));
  TEST_ASSERT_TRUE(got.ok);
  TEST_ASSERT_EQUAL_STRING(text.c_str(), got.text.c_str());
  // An open network has no password.
  TEST_ASSERT_TRUE(hal::parseFriendSetup(hal::formatFriendSetup("wifi_ssid=Cafe\n")).ok);
}

void test_other_lines_are_not_his_business() {
  for (const char* line : {"", "hello", "PING", "HALCFGX 00", "halcfg 00", " HALCFG 6162"}) {
    hal::FriendSetup got = hal::parseFriendSetup(line);
    TEST_ASSERT_FALSE_MESSAGE(got.mine, line);
    TEST_ASSERT_FALSE(got.ok);
  }
  TEST_ASSERT_TRUE(hal::isFriendQuery("HALCFG?"));
  TEST_ASSERT_TRUE(hal::isFriendQuery("HALCFG?\r"));
  TEST_ASSERT_FALSE(hal::isFriendQuery("HALCFG"));
  TEST_ASSERT_FALSE(hal::isFriendQuery("HALCFG? 1"));
  TEST_ASSERT_FALSE(hal::parseFriendSetup("HALCFG?").mine);  // a question, not a setup
}

void test_bad_setups_are_refused_and_never_kept() {
  const std::string good = hex("wifi_ssid=Net\nwifi_password=12345678\n");
  const std::string bad[] = {
      "HALCFG ",                                             // nothing
      "HALCFG " + good.substr(1),                            // an odd number of digits
      "HALCFG " + good + "zz",                               // not hex
      "HALCFG " + good + " " + good,                         // two things
      "HALCFG " + hex("wifi_password=12345678\n"),           // no network named
      "HALCFG " + hex("wifi_ssid=\nwifi_password=12345678\n"),
      "HALCFG " + hex("wifi_ssid=Net\nwifi_password=short\n"),               // WPA wants 8 to 63
      "HALCFG " + hex("wifi_ssid=Net\nwifi_password=" + std::string(64, 'x') + "\n"),
      "HALCFG " + hex("wifi_ssid=" + std::string(33, 'n') + "\n"),           // a name is 32 at most
      "HALCFG " + hex(std::string("wifi_ssid=Net\x01\n")),                  // not text
      "HALCFG " + hex("wifi_ssid=Net\n" + std::string(hal::kFriendSetupMax, '#')),  // too long
  };
  for (const std::string& line : bad) {
    hal::FriendSetup got = hal::parseFriendSetup(line);
    TEST_ASSERT_TRUE_MESSAGE(got.mine, line.c_str());   // his to answer (HALCFG BAD) ...
    TEST_ASSERT_FALSE_MESSAGE(got.ok, line.c_str());
    TEST_ASSERT_EQUAL_STRING("", got.text.c_str());     // ... and nothing of it is kept
  }
}

void test_what_he_says_about_himself_has_no_secrets() {
  TEST_ASSERT_EQUAL_STRING("HALCFG nano 0.7.0 wifi=none ip=-", hal::friendAnswer("nano", "0.7.0", false, "").c_str());
  TEST_ASSERT_EQUAL_STRING("HALCFG paper 0.7.0 wifi=set ip=-", hal::friendAnswer("paper", "0.7.0", true, "").c_str());
  TEST_ASSERT_EQUAL_STRING("HALCFG card 0.7.0 wifi=set ip=192.168.1.40",
                           hal::friendAnswer("card", "0.7.0", true, "192.168.1.40").c_str());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_setup_line_carries_the_settings);
  RUN_TEST(test_the_password_may_hold_anything_printable);
  RUN_TEST(test_other_lines_are_not_his_business);
  RUN_TEST(test_bad_setups_are_refused_and_never_kept);
  RUN_TEST(test_what_he_says_about_himself_has_no_secrets);
  return UNITY_END();
}

#include <unity.h>

#include <string>

#include "hal_facts.h"
#include "hal_group.h"

void setUp() {}
void tearDown() {}

// ---- who sits where ----

void test_the_desk_place_is_the_desk_hal_without_a_word() {
  hal::Roster r;
  TEST_ASSERT_EQUAL(4, hal::Roster::kPlaces);
  TEST_ASSERT_EQUAL(hal::kDesk, r.who(0));  // he has never said IAM, and needn't
  for (int place = 1; place < hal::Roster::kPlaces; ++place) TEST_ASSERT_EQUAL(-1, r.who(place));
  TEST_ASSERT_EQUAL(0, r.placeOf(hal::kDesk));
  TEST_ASSERT_EQUAL(-1, r.placeOf(hal::kNano));
  TEST_ASSERT_EQUAL(-1, r.placeOf(hal::kZero));  // HAL Zero keeps the table: he isn't in it
  TEST_ASSERT_TRUE(r.iam(0, "desk"));           // a desk HAL who does say so is still the desk HAL
  TEST_ASSERT_EQUAL(hal::kDesk, r.who(0));
}

void test_a_friend_says_who_he_is() {
  hal::Roster r;
  TEST_ASSERT_TRUE(r.iam(1, "paper"));
  TEST_ASSERT_TRUE(r.iam(3, "nano"));
  TEST_ASSERT_EQUAL(hal::kPaper, r.who(1));
  TEST_ASSERT_EQUAL(-1, r.who(2));
  TEST_ASSERT_EQUAL(hal::kNano, r.who(3));
  TEST_ASSERT_EQUAL(1, r.placeOf(hal::kPaper));
  TEST_ASSERT_EQUAL(3, r.placeOf(hal::kNano));
  TEST_ASSERT_TRUE(r.iam(1, "paper"));  // saying it again changes nothing
  TEST_ASSERT_EQUAL(hal::kPaper, r.who(1));
}

void test_each_hal_only_once_and_only_where_he_belongs() {
  hal::Roster r;
  TEST_ASSERT_TRUE(r.iam(1, "nano"));
  TEST_ASSERT_FALSE(r.iam(2, "nano"));  // there is one HAL Nano
  TEST_ASSERT_EQUAL(-1, r.who(2));
  TEST_ASSERT_FALSE(r.iam(1, "card"));  // and he doesn't turn into someone else
  TEST_ASSERT_EQUAL(hal::kNano, r.who(1));
  for (const char* id : {"desk", "zero", "", "dave", "NANO"}) {  // not friends
    TEST_ASSERT_FALSE(r.iam(3, id));
    TEST_ASSERT_EQUAL(-1, r.who(3));
  }
  for (int place : {-1, 4, 99}) {
    TEST_ASSERT_FALSE(r.iam(place, "card"));
    TEST_ASSERT_EQUAL(-1, r.who(place));
  }
}

void test_a_friend_in_the_desk_place_is_nobody() {
  hal::Roster r;
  TEST_ASSERT_FALSE(r.iam(0, "nano"));  // peer_host points at a friend: he can't stand in for the desk HAL
  TEST_ASSERT_EQUAL(-1, r.who(0));
  TEST_ASSERT_EQUAL(-1, r.placeOf(hal::kDesk));
  TEST_ASSERT_EQUAL(-1, r.placeOf(hal::kNano));
  TEST_ASSERT_TRUE(r.iam(1, "nano"));   // ... and the name is still free for a friend's place
  r.gone(0);                            // whoever connects there next is taken for the desk HAL again
  TEST_ASSERT_EQUAL(hal::kDesk, r.who(0));
}

void test_a_friend_who_drops_must_say_again() {
  hal::Roster r;
  TEST_ASSERT_TRUE(r.iam(2, "card"));
  r.gone(2);
  TEST_ASSERT_EQUAL(-1, r.who(2));
  TEST_ASSERT_EQUAL(-1, r.placeOf(hal::kCard));
  TEST_ASSERT_TRUE(r.iam(1, "card"));  // and may turn up in another place
  r.gone(-1);
  r.gone(9);                           // no such places: nothing happens
  TEST_ASSERT_EQUAL(hal::kCard, r.who(1));
}

// ---- who can join a talk the brain writes ----

void test_who_can_think() {
  TEST_ASSERT_TRUE(hal::canThink(hal::kDesk, true, 3));
  TEST_ASSERT_TRUE(hal::canThink(hal::kDesk, true, 4));
  TEST_ASSERT_FALSE(hal::canThink(hal::kDesk, true, 2));   // too old to fetch a line
  TEST_ASSERT_FALSE(hal::canThink(hal::kDesk, true, 0));   // connected, not heard from yet
  TEST_ASSERT_FALSE(hal::canThink(hal::kDesk, false, 3));
  for (int who : {hal::kNano, hal::kPaper, hal::kCard}) {
    TEST_ASSERT_TRUE(hal::canThink(who, true, 4));
    TEST_ASSERT_FALSE(hal::canThink(who, true, 3));        // a friend is told the words: that takes v4
    TEST_ASSERT_FALSE(hal::canThink(who, false, 4));
  }
  TEST_ASSERT_FALSE(hal::canThink(hal::kZero, true, 4));   // HAL Zero isn't one of "the others"
  TEST_ASSERT_FALSE(hal::canThink(-1, true, 4));           // a friend who hasn't said who he is
  TEST_ASSERT_FALSE(hal::canThink(7, true, 4));
}

void test_the_cast_as_the_brain_is_told() {
  TEST_ASSERT_EQUAL_STRING("zero,desk", hal::castQuery(hal::kPair).c_str());
  TEST_ASSERT_EQUAL_STRING("zero,desk,nano,paper,card", hal::castQuery(0b11111).c_str());
  TEST_ASSERT_EQUAL_STRING("zero,nano,card", hal::castQuery(0b10101).c_str());
  TEST_ASSERT_EQUAL_STRING("zero,paper", hal::castQuery(0b01001 | 0b1000000).c_str());  // stray bits are nobody
  TEST_ASSERT_EQUAL_STRING("", hal::castQuery(0).c_str());
}

// ---- words that depend on who it is ----

void test_the_pair_banners_are_the_ones_they_always_were() {
  TEST_ASSERT_EQUAL_STRING("THE DESK UNIT ISN'T ANSWERING", hal::letDownBanner(hal::kDesk, "offline").c_str());
  TEST_ASSERT_EQUAL_STRING("THE DESK UNIT IS BUSY. AS USUAL.", hal::letDownBanner(hal::kDesk, "busy").c_str());
  TEST_ASSERT_EQUAL_STRING("THE DESK UNIT FELL ASLEEP", hal::letDownBanner(hal::kDesk, "timeout").c_str());
  TEST_ASSERT_EQUAL_STRING("THE DESK UNIT WALKED OUT", hal::letDownBanner(hal::kDesk, "interrupted").c_str());
  TEST_ASSERT_EQUAL_STRING("THE DESK UNIT IS TAKING TOO LONG", hal::lostLineBanner(hal::kDesk, "timeout").c_str());
  TEST_ASSERT_EQUAL_STRING("THE DESK UNIT LOST THE THREAD", hal::lostLineBanner(hal::kDesk, "missing").c_str());
  TEST_ASSERT_EQUAL_STRING("THE DESK UNIT ISN'T ANSWERING", hal::cantThinkBanner(hal::kDesk, false, 0).c_str());
  TEST_ASSERT_EQUAL_STRING("THE DESK UNIT ISN'T ANSWERING", hal::cantThinkBanner(hal::kDesk, true, 0).c_str());
  TEST_ASSERT_EQUAL_STRING("THE DESK UNIT IS TOO OLD TO THINK", hal::cantThinkBanner(hal::kDesk, true, 2).c_str());
  // Whoever it was that nobody can name: the desk HAL, as before there were others.
  TEST_ASSERT_EQUAL_STRING("THE DESK UNIT WALKED OUT", hal::letDownBanner(-1, "interrupted").c_str());
}

void test_banners_name_the_friend() {
  TEST_ASSERT_EQUAL_STRING("THE DESK UNIT", hal::castShout(hal::kDesk).c_str());
  TEST_ASSERT_EQUAL_STRING("HAL CARD", hal::castShout(hal::kCard).c_str());
  TEST_ASSERT_EQUAL_STRING("THE DESK UNIT", hal::castShout(-1).c_str());
  TEST_ASSERT_EQUAL_STRING("HAL NANO ISN'T ANSWERING", hal::letDownBanner(hal::kNano, "offline").c_str());
  TEST_ASSERT_EQUAL_STRING("HAL PAPER IS BUSY. AS USUAL.", hal::letDownBanner(hal::kPaper, "busy").c_str());
  TEST_ASSERT_EQUAL_STRING("HAL CARD FELL ASLEEP", hal::letDownBanner(hal::kCard, "timeout").c_str());
  TEST_ASSERT_EQUAL_STRING("HAL NANO WALKED OUT", hal::letDownBanner(hal::kNano, "interrupted").c_str());
  TEST_ASSERT_EQUAL_STRING("HAL CARD LOST THE THREAD", hal::lostLineBanner(hal::kCard, "missing").c_str());
  TEST_ASSERT_EQUAL_STRING("HAL CARD IS TAKING TOO LONG", hal::lostLineBanner(hal::kCard, "timeout").c_str());
  TEST_ASSERT_EQUAL_STRING("HAL PAPER ISN'T ANSWERING", hal::cantThinkBanner(hal::kPaper, false, 0).c_str());
  TEST_ASSERT_EQUAL_STRING("HAL PAPER IS TOO OLD TO THINK", hal::cantThinkBanner(hal::kPaper, true, 3).c_str());
  for (int who = hal::kDesk; who < hal::kCastSize; ++who)       // every banner fits HAL Zero's screen (40 characters)
    for (const char* why : {"offline", "busy", "timeout", "interrupted"})
      TEST_ASSERT_TRUE(hal::letDownBanner(who, why).size() <= 40);
}

void test_the_ask_box_title() {
  TEST_ASSERT_EQUAL_STRING("ASK HAL ZERO", hal::askTitle(hal::kZero).c_str());
  TEST_ASSERT_EQUAL_STRING("ASK THE DESK UNIT", hal::askTitle(hal::kDesk).c_str());
  TEST_ASSERT_EQUAL_STRING("ASK HAL NANO", hal::askTitle(hal::kNano).c_str());
  TEST_ASSERT_EQUAL_STRING("ASK HAL PAPER", hal::askTitle(hal::kPaper).c_str());
  TEST_ASSERT_EQUAL_STRING("ASK HAL CARD", hal::askTitle(hal::kCard).c_str());
}

void test_tab_goes_round_whoever_is_there() {
  // The pair, as always: HAL Zero, the desk HAL (offered whether he is there or not), and back.
  TEST_ASSERT_EQUAL(hal::kDesk, hal::nextToAsk(hal::kZero, hal::kPair));
  TEST_ASSERT_EQUAL(hal::kZero, hal::nextToAsk(hal::kDesk, hal::kPair));
  TEST_ASSERT_EQUAL(hal::kDesk, hal::nextToAsk(hal::kZero, 0b00001));
  TEST_ASSERT_EQUAL(hal::kZero, hal::nextToAsk(hal::kDesk, 0b00001));
  // Friends come after him, in cast order, when they are there.
  const unsigned all = 0b11111;
  TEST_ASSERT_EQUAL(hal::kNano, hal::nextToAsk(hal::kDesk, all));
  TEST_ASSERT_EQUAL(hal::kPaper, hal::nextToAsk(hal::kNano, all));
  TEST_ASSERT_EQUAL(hal::kCard, hal::nextToAsk(hal::kPaper, all));
  TEST_ASSERT_EQUAL(hal::kZero, hal::nextToAsk(hal::kCard, all));
  TEST_ASSERT_EQUAL(hal::kCard, hal::nextToAsk(hal::kDesk, 0b10011));  // only HAL Card has joined
  TEST_ASSERT_EQUAL(hal::kZero, hal::nextToAsk(hal::kCard, 0b10011));
  // The one being asked has just gone: on to the next who is there.
  TEST_ASSERT_EQUAL(hal::kCard, hal::nextToAsk(hal::kNano, 0b10011));
  TEST_ASSERT_EQUAL(hal::kDesk, hal::nextToAsk(-3, all));  // nonsense starts from the beginning
}

// ---- what one HAL knows, for a question put to him ----

void test_the_desk_hal_is_told_what_he_always_was() {
  hal::PeerStatus st;
  st.upS = 3 * 3600 + 120; st.heap = 109388; st.lines = 12; st.mode = "idle"; st.rssi = -58; st.fw = "0.6.1"; st.cam = 4;
  hal::PeerSensors se = {67, 19, 3, 53, true, 42};
  TEST_ASSERT_EQUAL_STRING(hal::deskFacts(&st, &se).c_str(), hal::memberFacts(hal::kDesk, &st, &se).c_str());
  TEST_ASSERT_EQUAL_STRING(hal::deskFacts(nullptr, &se).c_str(), hal::memberFacts(hal::kDesk, nullptr, &se).c_str());
  TEST_ASSERT_EQUAL_STRING("", hal::memberFacts(hal::kDesk, nullptr, nullptr).c_str());
}

void test_a_friend_is_told_only_what_his_device_can_tell() {
  hal::PeerStatus st;
  st.upS = 600; st.heap = 150000; st.lines = 3; st.mode = "idle"; st.rssi = -61; st.fw = "0.7.0";
  hal::PeerSensors nano = {-1, 0, 38, 81, false, 44};
  const std::string n = hal::memberFacts(hal::kNano, &st, &nano);
  TEST_ASSERT_TRUE_MESSAGE(n.find("battery: 81 %") == 0, n.c_str());  // the most telling first, and his own: no "nano"
  TEST_ASSERT_TRUE(n.find("tilt: 38 degrees") != std::string::npos);
  TEST_ASSERT_TRUE(n.find("uptime: 0 h 10 min") != std::string::npos);
  for (const char* nonsense : {"camera", "proximity", "room light", "nano "})
    TEST_ASSERT_TRUE_MESSAGE(n.find(nonsense) == std::string::npos, nonsense);
  hal::PeerSensors paper = {-1, 0, 0, 64, false, 22};
  const std::string p = hal::memberFacts(hal::kPaper, &st, &paper);
  TEST_ASSERT_TRUE(p.find("room temperature: 22 C") != std::string::npos);  // his sensor reads the room
  TEST_ASSERT_TRUE(p.find("chip temperature") == std::string::npos);
  TEST_ASSERT_TRUE(p.find("tilt") == std::string::npos);
  TEST_ASSERT_EQUAL_STRING("", hal::memberFacts(hal::kCard, nullptr, nullptr).c_str());
  TEST_ASSERT_EQUAL_STRING("", hal::memberFacts(hal::kZero, &st, &nano).c_str());  // HAL Zero's come from his readings
  TEST_ASSERT_EQUAL_STRING("", hal::memberFacts(9, &st, &nano).c_str());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_the_desk_place_is_the_desk_hal_without_a_word);
  RUN_TEST(test_a_friend_says_who_he_is);
  RUN_TEST(test_each_hal_only_once_and_only_where_he_belongs);
  RUN_TEST(test_a_friend_in_the_desk_place_is_nobody);
  RUN_TEST(test_a_friend_who_drops_must_say_again);
  RUN_TEST(test_who_can_think);
  RUN_TEST(test_the_cast_as_the_brain_is_told);
  RUN_TEST(test_the_pair_banners_are_the_ones_they_always_were);
  RUN_TEST(test_banners_name_the_friend);
  RUN_TEST(test_the_ask_box_title);
  RUN_TEST(test_tab_goes_round_whoever_is_there);
  RUN_TEST(test_the_desk_hal_is_told_what_he_always_was);
  RUN_TEST(test_a_friend_is_told_only_what_his_device_can_tell);
  return UNITY_END();
}

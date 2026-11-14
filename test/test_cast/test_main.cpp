#include <unity.h>

#include <string>

#include "hal_cast.h"

void setUp() {}
void tearDown() {}

void test_the_five_in_their_fixed_order() {
  TEST_ASSERT_EQUAL(5, hal::kCastSize);
  const char* const ids[] = {"zero", "desk", "nano", "paper", "card"};
  for (int who = 0; who < hal::kCastSize; ++who) {
    TEST_ASSERT_EQUAL_STRING(ids[who], hal::castId(who));
    TEST_ASSERT_EQUAL(who, hal::castFromId(ids[who]));
    TEST_ASSERT_TRUE(std::string(hal::castName(who)).size() > 3);
  }
  TEST_ASSERT_EQUAL(0, hal::kZero);
  TEST_ASSERT_EQUAL(1, hal::kDesk);
  TEST_ASSERT_EQUAL(2, hal::kNano);
  TEST_ASSERT_EQUAL(3, hal::kPaper);
  TEST_ASSERT_EQUAL(4, hal::kCard);
}

void test_strangers_are_nobody() {
  for (const char* id : {"", "dave", "ZERO", "Zero", "desk ", "hal", "nan"}) TEST_ASSERT_EQUAL(-1, hal::castFromId(id));
  for (int who : {-1, 5, 99}) {
    TEST_ASSERT_EQUAL_STRING("", hal::castId(who));
    TEST_ASSERT_EQUAL_STRING("", hal::castName(who));
    TEST_ASSERT_FALSE(hal::castVoiced(who));
  }
}

void test_names_on_screen() {
  // What the subtitles call each of them (the first two as they have been since v0.3).
  TEST_ASSERT_EQUAL_STRING("HAL ZERO", hal::castName(hal::kZero));
  TEST_ASSERT_EQUAL_STRING("DESK UNIT", hal::castName(hal::kDesk));
  TEST_ASSERT_EQUAL_STRING("HAL NANO", hal::castName(hal::kNano));
  TEST_ASSERT_EQUAL_STRING("HAL PAPER", hal::castName(hal::kPaper));
  TEST_ASSERT_EQUAL_STRING("HAL CARD", hal::castName(hal::kCard));
}

void test_who_has_a_voice() {
  TEST_ASSERT_TRUE(hal::castVoiced(hal::kZero));
  TEST_ASSERT_TRUE(hal::castVoiced(hal::kDesk));
  TEST_ASSERT_TRUE(hal::castVoiced(hal::kCard));
  TEST_ASSERT_FALSE(hal::castVoiced(hal::kNano));   // he chirps
  TEST_ASSERT_FALSE(hal::castVoiced(hal::kPaper));  // he writes
}

void test_who_is_present_is_a_set_of_bits() {
  TEST_ASSERT_EQUAL(0b00011, int(hal::kPair));
  TEST_ASSERT_EQUAL(0b00001, int(hal::castBit(hal::kZero)));
  TEST_ASSERT_EQUAL(0b10000, int(hal::castBit(hal::kCard)));
  TEST_ASSERT_EQUAL(0, int(hal::castBit(-1)));
  TEST_ASSERT_EQUAL(0, int(hal::castBit(5)));
  TEST_ASSERT_EQUAL(2, hal::castCount(hal::kPair));
  TEST_ASSERT_EQUAL(5, hal::castCount(0b11111));
  TEST_ASSERT_EQUAL(3, hal::castCount(0b10101 | 0b100000));  // bits beyond the cast don't count
  TEST_ASSERT_TRUE(hal::castHas(hal::kPair, hal::kDesk));
  TEST_ASSERT_FALSE(hal::castHas(hal::kPair, hal::kNano));
  TEST_ASSERT_FALSE(hal::castHas(0b11111, 7));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_the_five_in_their_fixed_order);
  RUN_TEST(test_strangers_are_nobody);
  RUN_TEST(test_names_on_screen);
  RUN_TEST(test_who_has_a_voice);
  RUN_TEST(test_who_is_present_is_a_set_of_bits);
  return UNITY_END();
}

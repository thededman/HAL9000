#include <unity.h>

#include "hal_menu.h"

void setUp() {}
void tearDown() {}

namespace {
hal::Menu three() {
  hal::Menu m;
  m.open({{"talk", "Talk to the desk unit"}, {"visual", "Visual analysis"}, {"daisy", "Sing"}});
  return m;
}
}  // namespace

void test_open_and_close() {
  hal::Menu m;
  TEST_ASSERT_FALSE(m.isOpen());
  m = three();
  TEST_ASSERT_TRUE(m.isOpen());
  TEST_ASSERT_EQUAL(0, m.selected());
  TEST_ASSERT_EQUAL_STRING("talk", m.selectedId().c_str());
  m.close();
  TEST_ASSERT_FALSE(m.isOpen());
}

void test_up_down_wrap() {
  hal::Menu m = three();
  m.down();
  TEST_ASSERT_EQUAL_STRING("visual", m.selectedId().c_str());
  m.down();
  m.down();
  TEST_ASSERT_EQUAL_STRING("talk", m.selectedId().c_str());  // wrapped
  m.up();
  TEST_ASSERT_EQUAL_STRING("daisy", m.selectedId().c_str());
}

void test_empty_menu_is_safe() {
  hal::Menu m;
  m.open({});
  m.down();
  m.up();
  TEST_ASSERT_EQUAL_STRING("", m.selectedId().c_str());
}

void test_scrolling_keeps_selection_visible() {
  hal::Menu m;
  std::vector<hal::MenuItem> items;
  for (int i = 0; i < 10; ++i) items.push_back({std::to_string(i), "item"});
  m.open(items);
  TEST_ASSERT_EQUAL(0, m.firstVisible(4));
  for (int i = 0; i < 5; ++i) m.down();  // selected 5
  TEST_ASSERT_EQUAL(2, m.firstVisible(4));  // rows 2..5
  for (int i = 0; i < 5; ++i) m.down();  // wraps to 0
  TEST_ASSERT_EQUAL(0, m.firstVisible(4));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_open_and_close);
  RUN_TEST(test_up_down_wrap);
  RUN_TEST(test_empty_menu_is_safe);
  RUN_TEST(test_scrolling_keeps_selection_visible);
  return UNITY_END();
}

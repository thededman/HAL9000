#include <unity.h>

#include <string>

#include "hal_procfs.h"

void setUp() {}
void tearDown() {}

void test_proc_stat() {
  const std::string text =
      "cpu  100 0 50 1000 10 0 5 0 0 0\n"
      "cpu0 25 0 10 250 5 0 2 0 0 0\n"
      "cpu1 25 0 15 250 0 0 1 0 0 0\n"
      "intr 12345\n";
  auto cpus = hal::parseProcStat(text);
  TEST_ASSERT_EQUAL(2, int(cpus.size()));
  TEST_ASSERT_EQUAL(37, int(cpus[0].busy));
  TEST_ASSERT_EQUAL(292, int(cpus[0].total));
  TEST_ASSERT_EQUAL(41, int(cpus[1].busy));
}

void test_cpu_percent_edges() {
  TEST_ASSERT_EQUAL(50, hal::cpuPercent({37, 292}, {87, 392}));
  TEST_ASSERT_EQUAL(0, hal::cpuPercent({37, 292}, {37, 292}));   // no time passed
  TEST_ASSERT_EQUAL(0, hal::cpuPercent({87, 392}, {37, 292}));   // counters went backwards
  TEST_ASSERT_EQUAL(100, hal::cpuPercent({0, 0}, {100, 100}));
  TEST_ASSERT_TRUE(hal::parseProcStat("garbage").empty());
}

void test_meminfo() {
  const std::string text = "MemTotal:         360448 kB\nMemFree: 12000 kB\nMemAvailable:     152000 kB\n";
  TEST_ASSERT_EQUAL(360448, hal::parseMeminfoKb(text, "MemTotal"));
  TEST_ASSERT_EQUAL(152000, hal::parseMeminfoKb(text, "MemAvailable"));
  TEST_ASSERT_EQUAL(-1, hal::parseMeminfoKb(text, "Nope"));
  TEST_ASSERT_EQUAL(-1, hal::parseMeminfoKb(text, "Mem"));  // whole names only
}

void test_wireless() {
  const std::string header =
      "Inter-| sta-|   Quality        |   Discarded packets               | Missed | WE\n"
      " face | tus | link level noise |  nwid  crypt   frag  retry   misc | beacon | 22\n";
  const std::string text = header + " wlan0: 0000   52.  -58.  -256        0      0      0      0     30        0\n";
  TEST_ASSERT_EQUAL(-58, hal::parseWirelessDbm(text));
  TEST_ASSERT_EQUAL(0, hal::parseWirelessDbm(header));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_proc_stat);
  RUN_TEST(test_cpu_percent_edges);
  RUN_TEST(test_meminfo);
  RUN_TEST(test_wireless);
  return UNITY_END();
}

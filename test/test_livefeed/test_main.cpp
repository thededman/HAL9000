#include <unity.h>

#include <string>

#include "hal_livefeed.h"

using hal::FeedOut;
using hal::LiveFeed;
using hal::Speaker;

void setUp() {}
void tearDown() {}

namespace {

const char* const kHost = "192.168.1.20";
const char* const kToken = "0123456789abcdef0123456789abcdef";

hal::Message msg(const char* line) { return hal::parseMessage(line); }

bool nothing(const FeedOut& o) { return !o.send && !o.append && !o.close && !o.failed; }

FeedOut deskLine(LiveFeed& f, const char* text, bool last, uint32_t now) {
  return f.onLine(Speaker::Old, text, last, kHost, 9100, kToken, now);
}

}  // namespace

void test_feed_local_line() {
  LiveFeed f;
  f.start();
  TEST_ASSERT_FALSE(f.busy());
  FeedOut o = f.onLine(Speaker::New, "I am the upgrade.", false, kHost, 9100, kToken, 1000);
  TEST_ASSERT_TRUE(nothing(o));  // his own audio is on its way: nothing to send
  TEST_ASSERT_TRUE(f.busy());
  TEST_ASSERT_EQUAL(1, f.pendingLine());
  o = f.onLocalClip();
  TEST_ASSERT_TRUE(o.append);
  TEST_ASSERT(o.who == Speaker::New);
  TEST_ASSERT_EQUAL_STRING("I am the upgrade.", o.text.c_str());
  TEST_ASSERT_FALSE(o.close);
  TEST_ASSERT_FALSE(f.busy());
  TEST_ASSERT_EQUAL(0, f.pendingLine());
}

void test_feed_desk_line() {
  LiveFeed f;
  f.start();
  FeedOut o = deskLine(f, "Good evening, Dave.", false, 1000);
  TEST_ASSERT_TRUE(o.send);
  TEST_ASSERT_EQUAL_STRING("FETCH 1 192.168.1.20 9100 0123456789abcdef0123456789abcdef", hal::formatMessage(o.msg).c_str());
  TEST_ASSERT_FALSE(o.append);  // not until he has it
  TEST_ASSERT_TRUE(f.busy());
  o = f.onRemote(msg("READY 1"));
  TEST_ASSERT_TRUE(o.append);
  TEST_ASSERT(o.who == Speaker::Old);
  TEST_ASSERT_EQUAL_STRING("Good evening, Dave.", o.text.c_str());
  TEST_ASSERT_FALSE(f.busy());
}

void test_feed_numbers_count_up() {
  LiveFeed f;
  f.start();
  f.onLine(Speaker::New, "One.", false, kHost, 9100, kToken, 0);
  f.onLocalClip();
  deskLine(f, "Two.", false, 100);
  f.onRemote(msg("READY 2"));
  f.onLine(Speaker::New, "Three.", false, kHost, 9100, kToken, 200);
  f.onLocalClip();
  FeedOut o = deskLine(f, "Four.", false, 300);
  TEST_ASSERT_EQUAL_STRING("FETCH 4 192.168.1.20 9100 0123456789abcdef0123456789abcdef", hal::formatMessage(o.msg).c_str());
  TEST_ASSERT_EQUAL(4, f.pendingLine());
}

void test_feed_last_closes() {
  LiveFeed f;
  f.start();
  f.onLine(Speaker::New, "One.", false, kHost, 9100, kToken, 0);
  TEST_ASSERT_FALSE(f.onLocalClip().close);
  deskLine(f, "The last word.", true, 100);
  FeedOut o = f.onRemote(msg("READY 2"));
  TEST_ASSERT_TRUE(o.append && o.close);

  f.start();  // and a single answer is first and last at once
  f.onLine(Speaker::New, "Just this.", true, kHost, 9100, kToken, 0);
  o = f.onLocalClip();
  TEST_ASSERT_TRUE(o.append && o.close);
}

void test_feed_missing() {
  LiveFeed f;
  f.start();
  deskLine(f, "You won't hear this.", false, 0);
  FeedOut o = f.onRemote(msg("MISSING 1"));
  TEST_ASSERT_TRUE(o.failed);
  TEST_ASSERT_EQUAL_STRING("missing", o.reason.c_str());
  TEST_ASSERT_FALSE(o.append);
  TEST_ASSERT_FALSE(f.busy());
}

void test_feed_ready_timeout() {
  LiveFeed f;
  f.start();
  deskLine(f, "Still downloading.", false, 1000);
  TEST_ASSERT_TRUE(nothing(f.tick(9000)));  // 8 s exactly: still waiting
  FeedOut o = f.tick(9001);
  TEST_ASSERT_TRUE(o.failed);
  TEST_ASSERT_EQUAL_STRING("timeout", o.reason.c_str());
  TEST_ASSERT_FALSE(f.busy());
  TEST_ASSERT_TRUE(nothing(f.tick(20000)));  // once
  TEST_ASSERT_TRUE(nothing(f.onRemote(msg("READY 1"))));  // too late

  f.start();  // his own lines have no such deadline here: the brain client has its own
  f.onLine(Speaker::New, "Mine.", false, kHost, 9100, kToken, 0);
  TEST_ASSERT_TRUE(nothing(f.tick(60000)));
}

void test_feed_ignores_stale() {
  LiveFeed f;
  f.start();
  TEST_ASSERT_TRUE(nothing(f.onRemote(msg("READY 1"))));  // nothing pending
  TEST_ASSERT_TRUE(nothing(f.onLocalClip()));
  f.onLine(Speaker::New, "One.", false, kHost, 9100, kToken, 0);
  f.onLocalClip();
  deskLine(f, "Two.", false, 0);
  f.onRemote(msg("READY 2"));
  deskLine(f, "Three, wait.", false, 0);  // hmm: two desk lines running is fine for the feed
  TEST_ASSERT_TRUE(nothing(f.onRemote(msg("READY 2"))));    // an answer about an earlier line
  TEST_ASSERT_TRUE(nothing(f.onRemote(msg("MISSING 2"))));
  TEST_ASSERT_TRUE(nothing(f.onRemote(msg("DONE 3"))));     // not the feed's business
  TEST_ASSERT_TRUE(nothing(f.onLocalClip()));               // it's a desk line: his audio isn't ours
  TEST_ASSERT_TRUE(nothing(f.onLine(Speaker::New, "Jumping the queue.", false, kHost, 9100, kToken, 0)));
  TEST_ASSERT_EQUAL(3, f.pendingLine());
  TEST_ASSERT_TRUE(f.onRemote(msg("READY 3")).append);
}

void test_feed_bad_fetch_details() {
  LiveFeed f;
  f.start();
  // Whatever the brain's address or token looks like, only what the link would accept is sent.
  FeedOut o = f.onLine(Speaker::Old, "x", false, "8.8.8.8", 9100, kToken, 0);
  TEST_ASSERT_TRUE(o.failed && !o.send);
  TEST_ASSERT_FALSE(f.busy());
  o = f.onLine(Speaker::Old, "x", false, kHost, 80, kToken, 0);
  TEST_ASSERT_TRUE(o.failed && !o.send);
  o = f.onLine(Speaker::Old, "x", false, kHost, 9100, "not-a-token", 0);
  TEST_ASSERT_TRUE(o.failed && !o.send);
  o = f.onLine(Speaker::Old, "x", false, "", 9100, kToken, 0);
  TEST_ASSERT_TRUE(o.failed && !o.send);
}

void test_feed_restart() {
  LiveFeed f;
  f.start();
  deskLine(f, "One.", false, 0);
  f.start();  // a new conversation: the old line is forgotten
  TEST_ASSERT_FALSE(f.busy());
  TEST_ASSERT_TRUE(nothing(f.onRemote(msg("READY 1"))));
  FeedOut o = deskLine(f, "One again.", false, 0);
  TEST_ASSERT_EQUAL(1, o.msg.n);
}

void test_feed_stops_at_99_lines() {
  LiveFeed f;
  f.start();
  for (int n = 1; n <= 99; ++n) {
    f.onLine(Speaker::New, "x", false, kHost, 9100, kToken, 0);
    TEST_ASSERT_TRUE(f.onLocalClip().append);
  }
  FeedOut o = f.onLine(Speaker::New, "one too many", false, kHost, 9100, kToken, 0);
  TEST_ASSERT_TRUE(o.failed);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_feed_local_line);
  RUN_TEST(test_feed_desk_line);
  RUN_TEST(test_feed_numbers_count_up);
  RUN_TEST(test_feed_last_closes);
  RUN_TEST(test_feed_missing);
  RUN_TEST(test_feed_ready_timeout);
  RUN_TEST(test_feed_ignores_stale);
  RUN_TEST(test_feed_bad_fetch_details);
  RUN_TEST(test_feed_restart);
  RUN_TEST(test_feed_stops_at_99_lines);
  return UNITY_END();
}

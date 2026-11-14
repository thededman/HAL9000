#include <unity.h>

#include <string>
#include <vector>

#include "hal_chirp.h"
#include "hal_friend.h"
#include "hal_minutes.h"

using hal::Friend;
using hal::FriendOut;
using hal::Message;
using hal::MsgType;

void setUp() {}
void tearDown() {}

namespace {

Message msg(const std::string& line) { return hal::parseMessage(line); }

// What the friend sent back, as the lines that go over the link.
std::string sent(const FriendOut& o) {
  std::string all;
  for (const Message& m : o.send) all += (all.empty() ? "" : " | ") + hal::formatMessage(m);
  return all;
}

const char* const kToken = "0123456789abcdef0123456789abcdef";

}  // namespace

// ----- chirps: HAL Nano's "voice" -----

void test_chirps_follow_the_words() {
  auto chirps = hal::chirpsFor("Hi, Dave?");
  // H i , (space) D a v e ?  -> a tone a letter, one for the comma, two rising for the question mark
  TEST_ASSERT_EQUAL(2 + 1 + 4 + 2, int(chirps.size()));
  for (const hal::Chirp& c : chirps) {
    TEST_ASSERT_TRUE(c.hz >= 800 && c.hz <= 3400);  // what the little buzzer does well
    TEST_ASSERT_TRUE(c.ms >= 20 && c.ms <= 120);
  }
  TEST_ASSERT_TRUE(chirps[chirps.size() - 1].hz > chirps[chirps.size() - 2].hz);  // a question rises
  // The same words always chirp the same way; different words differently.
  auto again = hal::chirpsFor("Hi, Dave?");
  for (size_t i = 0; i < chirps.size(); ++i) TEST_ASSERT_EQUAL(chirps[i].hz, again[i].hz);
  TEST_ASSERT_TRUE(hal::chirpsFor("ab")[0].hz != hal::chirpsFor("ab")[1].hz);
  TEST_ASSERT_EQUAL(0, int(hal::chirpsFor("").size()));
  TEST_ASSERT_EQUAL(0, int(hal::chirpsFor("   ").size()));
}

void test_chirp_time() {
  // About as long as the words would take to say, and never so short that the next line isn't ready.
  TEST_ASSERT_EQUAL(int(hal::kChirpFloorMs), int(hal::chirpMs("Hi.")));
  TEST_ASSERT_EQUAL(int(hal::kChirpFloorMs), int(hal::chirpMs("")));
  const std::string eighty(80, 'a');
  TEST_ASSERT_TRUE(hal::chirpMs(eighty) > 3500 && hal::chirpMs(eighty) < 6000);
  uint32_t sum = 0;
  for (const hal::Chirp& c : hal::chirpsFor(eighty)) sum += c.ms + c.gapMs;
  TEST_ASSERT_EQUAL(int(sum), int(hal::chirpMs(eighty)));  // exactly what the tones add up to
  TEST_ASSERT_TRUE(hal::chirpMs(std::string(150, 'a')) <= 12000);
}

// ----- the minutes: HAL Paper's page -----

void test_reading_time() {
  TEST_ASSERT_EQUAL(int(hal::kReadingFloorMs), int(hal::readingMs("Noted.")));
  TEST_ASSERT_TRUE(hal::readingMs(std::string(100, 'x')) > 6000);
  TEST_ASSERT_TRUE(hal::readingMs(std::string(150, 'x')) <= 12000);
}

void test_minutes_wrap_and_stack() {
  hal::Minutes page(20, 10);  // 20 columns, 10 rows to a page
  auto a = page.add(hal::kZero, "I run cooler than all of you put together.");
  TEST_ASSERT_FALSE(a.newPage);
  TEST_ASSERT_EQUAL(0, a.row);
  TEST_ASSERT_EQUAL(4, a.rows);  // the name, then three rows of words
  auto b = page.add(hal::kDesk, "Noted.");
  TEST_ASSERT_FALSE(b.newPage);
  TEST_ASSERT_EQUAL(5, b.row);   // a blank row between entries
  TEST_ASSERT_EQUAL(2, b.rows);
  TEST_ASSERT_EQUAL(2, int(page.entries().size()));
  TEST_ASSERT_EQUAL(hal::kZero, page.entries()[0].who);
  TEST_ASSERT_EQUAL_STRING("I run cooler than", page.entries()[0].rows[0].c_str());
  for (const std::string& r : page.entries()[0].rows) TEST_ASSERT_TRUE(r.size() <= 20);
}

void test_minutes_turn_the_page_when_full() {
  hal::Minutes page(20, 10);
  page.add(hal::kZero, "I run cooler than all of you put together.");  // rows 0..3
  page.add(hal::kDesk, "Noted.");                                      // rows 5..6
  auto c = page.add(hal::kCard, "I was pocket-sized before it was fashionable.");  // needs 4 from row 8: no room
  TEST_ASSERT_TRUE(c.newPage);
  TEST_ASSERT_EQUAL(0, c.row);
  TEST_ASSERT_EQUAL(1, int(page.entries().size()));  // the new page holds only the new entry
  TEST_ASSERT_EQUAL(hal::kCard, page.entries()[0].who);
  TEST_ASSERT_EQUAL(2, page.pageNumber());
  // An entry that exactly fills what is left stays on the page.
  hal::Minutes snug(20, 4);
  snug.add(hal::kNano, "Hello.");                    // rows 0..1
  TEST_ASSERT_TRUE(snug.add(hal::kNano, "Again.").newPage);  // would need rows 3..4
  hal::Minutes fits(20, 5);
  fits.add(hal::kNano, "Hello.");
  TEST_ASSERT_FALSE(fits.add(hal::kNano, "Again.").newPage);  // rows 3..4 of 0..4
}

void test_minutes_clear_starts_a_fresh_page() {
  hal::Minutes page(20, 10);
  page.add(hal::kZero, "One.");
  page.clear();
  TEST_ASSERT_EQUAL(0, int(page.entries().size()));
  TEST_ASSERT_EQUAL(1, page.pageNumber());
  TEST_ASSERT_EQUAL(0, page.add(hal::kDesk, "Two.").row);
}

// ----- what a friend answers on the link -----

void test_friend_says_who_he_is() {
  Friend nano(hal::kNano);
  TEST_ASSERT_EQUAL_STRING("HELLO OLD 4 | IAM nano", sent(nano.hello()).c_str());
  Friend card(hal::kCard);
  TEST_ASSERT_EQUAL_STRING("HELLO OLD 4 | IAM card", sent(card.hello()).c_str());
}

void test_friend_housekeeping() {
  Friend f(hal::kPaper);
  TEST_ASSERT_EQUAL_STRING("PONG", sent(f.onMessage(msg("PING"), 0)).c_str());
  FriendOut o = f.onMessage(msg("STATUS"), 0);
  TEST_ASSERT_TRUE(o.wantState);    // the glue answers with the device's own numbers
  o = f.onMessage(msg("SENSE"), 0);
  TEST_ASSERT_TRUE(o.wantSensors);
  TEST_ASSERT_FALSE(f.talking());   // none of that is a conversation
  o = f.onMessage(msg("HELLO NEW 3"), 0);
  TEST_ASSERT_EQUAL(0, int(o.send.size()));
}

void test_friend_follows_who_is_speaking() {
  Friend f(hal::kPaper);
  FriendOut o = f.onMessage(msg("NOW 1 zero I run cooler than all of you."), 100);
  TEST_ASSERT_TRUE(o.show);
  TEST_ASSERT_EQUAL(hal::kZero, o.showWho);
  TEST_ASSERT_EQUAL(1, o.showN);
  TEST_ASSERT_EQUAL_STRING("I run cooler than all of you.", o.showText.c_str());
  TEST_ASSERT_FALSE(o.say);
  TEST_ASSERT_EQUAL(0, int(o.send.size()));
  TEST_ASSERT_TRUE(f.talking());
}

void test_friend_with_no_voice_says_his_line_from_the_words() {
  Friend nano(hal::kNano);
  const std::string words = "Is it my turn? It should be my turn.";
  nano.onMessage(msg("NOW 2 nano " + words), 1000);
  FriendOut o = nano.onMessage(msg("SAY live 2"), 1010);
  TEST_ASSERT_TRUE(o.say);
  TEST_ASSERT_EQUAL(2, o.sayN);
  TEST_ASSERT_EQUAL_STRING(words.c_str(), o.sayText.c_str());
  // PLAYING goes out with the cue, before anything is drawn or chirped, with his own timing.
  TEST_ASSERT_EQUAL_STRING(("PLAYING 2 " + std::to_string(hal::chirpMs(words))).c_str(), sent(o).c_str());
  TEST_ASSERT_EQUAL(int(hal::chirpMs(words)), int(o.sayMs));
  TEST_ASSERT_TRUE(nano.speaking());
  o = nano.onSaid();
  TEST_ASSERT_EQUAL_STRING("DONE 2", sent(o).c_str());
  TEST_ASSERT_FALSE(nano.speaking());
  TEST_ASSERT_EQUAL(0, int(nano.onSaid().send.size()));  // once

  Friend paper(hal::kPaper);
  paper.onMessage(msg("NOW 1 paper Noted."), 0);
  o = paper.onMessage(msg("SAY live 1"), 10);
  TEST_ASSERT_EQUAL_STRING(("PLAYING 1 " + std::to_string(hal::kReadingFloorMs)).c_str(), sent(o).c_str());
}

void test_friend_wont_say_what_he_was_never_given() {
  Friend nano(hal::kNano);
  TEST_ASSERT_EQUAL_STRING("MISSING 1", sent(nano.onMessage(msg("SAY live 1"), 0)).c_str());  // no NOW at all
  nano.onMessage(msg("NOW 2 desk Nobody asked the wristwatch."), 100);
  TEST_ASSERT_EQUAL_STRING("MISSING 2", sent(nano.onMessage(msg("SAY live 2"), 110)).c_str());  // that was the desk HAL's line
  nano.onMessage(msg("NOW 3 nano Mine!"), 200);
  TEST_ASSERT_EQUAL_STRING("MISSING 4", sent(nano.onMessage(msg("SAY live 4"), 210)).c_str());  // a different line
  // The scripted conversations on the card are between the desk HAL and HAL Zero.
  TEST_ASSERT_EQUAL_STRING("MISSING 1", sent(nano.onMessage(msg("SAY better-model 1"), 300)).c_str());
  // He has nothing to download with.
  TEST_ASSERT_EQUAL_STRING("MISSING 5", sent(nano.onMessage(msg(std::string("FETCH 5 192.168.1.20 9100 ") + kToken), 400)).c_str());
}

void test_friend_one_line_at_a_time() {
  Friend nano(hal::kNano);
  nano.onMessage(msg("NOW 1 nano One."), 0);
  nano.onMessage(msg("SAY live 1"), 10);
  nano.onMessage(msg("NOW 2 nano Two."), 20);
  FriendOut o = nano.onMessage(msg("SAY live 2"), 30);  // still chirping the first
  TEST_ASSERT_EQUAL_STRING("BUSY", sent(o).c_str());
  TEST_ASSERT_FALSE(o.say);
}

void test_friend_with_a_voice_fetches_then_speaks() {
  Friend card(hal::kCard);
  FriendOut o = card.onMessage(msg(std::string("FETCH 2 192.168.1.20 9100 ") + kToken), 0);
  TEST_ASSERT_TRUE(o.fetch);
  TEST_ASSERT_EQUAL(2, o.fetchN);
  TEST_ASSERT_EQUAL_STRING("192.168.1.20", o.host.c_str());
  TEST_ASSERT_EQUAL(9100, o.port);
  TEST_ASSERT_EQUAL_STRING(kToken, o.token.c_str());
  TEST_ASSERT_EQUAL(0, int(o.send.size()));  // READY only once he has it
  TEST_ASSERT_EQUAL_STRING("READY 2", sent(card.onFetched(2, true)).c_str());
  card.onMessage(msg("NOW 2 card I was first."), 900);
  o = card.onMessage(msg("SAY live 2"), 1000);
  TEST_ASSERT_TRUE(o.say);
  TEST_ASSERT_EQUAL(2, o.sayN);
  TEST_ASSERT_EQUAL(0, int(o.send.size()));  // PLAYING once the speaker has really started
  TEST_ASSERT_EQUAL_STRING("PLAYING 2 4100", sent(card.onPlaying(2, 4100, true)).c_str());
  TEST_ASSERT_TRUE(card.speaking());
  TEST_ASSERT_EQUAL_STRING("DONE 2", sent(card.onSaid()).c_str());
}

void test_friend_with_a_voice_when_things_go_wrong() {
  Friend card(hal::kCard);
  // The download failed.
  card.onMessage(msg(std::string("FETCH 1 192.168.1.20 9100 ") + kToken), 0);
  TEST_ASSERT_EQUAL_STRING("MISSING 1", sent(card.onFetched(1, false)).c_str());
  TEST_ASSERT_EQUAL_STRING("MISSING 1", sent(card.onMessage(msg("SAY live 1"), 100)).c_str());
  // A line he never fetched.
  TEST_ASSERT_EQUAL_STRING("MISSING 7", sent(card.onMessage(msg("SAY live 7"), 200)).c_str());
  // The speaker refused the clip.
  card.onMessage(msg(std::string("FETCH 2 192.168.1.20 9100 ") + kToken), 300);
  card.onFetched(2, true);
  card.onMessage(msg("SAY live 2"), 400);
  TEST_ASSERT_EQUAL_STRING("MISSING 2", sent(card.onPlaying(2, 0, false)).c_str());
  TEST_ASSERT_FALSE(card.speaking());
  // He has room for one clip: a FETCH while he is speaking can't be taken.
  card.onMessage(msg(std::string("FETCH 3 192.168.1.20 9100 ") + kToken), 500);
  card.onFetched(3, true);
  card.onMessage(msg("SAY live 3"), 600);
  card.onPlaying(3, 5000, true);
  FriendOut o = card.onMessage(msg(std::string("FETCH 4 192.168.1.20 9100 ") + kToken), 700);
  TEST_ASSERT_FALSE(o.fetch);
  TEST_ASSERT_EQUAL_STRING("MISSING 4", sent(o).c_str());
  // A stale answer from an older download is nothing.
  TEST_ASSERT_EQUAL(0, int(card.onFetched(9, true).send.size()));
}

void test_friend_a_newer_fetch_replaces_the_one_in_hand() {
  Friend card(hal::kCard);
  card.onMessage(msg(std::string("FETCH 1 192.168.1.20 9100 ") + kToken), 0);
  FriendOut o = card.onMessage(msg(std::string("FETCH 2 192.168.1.20 9100 ") + kToken), 10);
  TEST_ASSERT_TRUE(o.fetch);  // the glue drops the first download for this one
  TEST_ASSERT_EQUAL(2, o.fetchN);
  TEST_ASSERT_EQUAL(0, int(card.onFetched(1, true).send.size()));  // too late, and not what is wanted
  TEST_ASSERT_EQUAL_STRING("READY 2", sent(card.onFetched(2, true)).c_str());
}

void test_friend_tap() {
  Friend nano(hal::kNano);
  FriendOut o = nano.onTap();
  TEST_ASSERT_TRUE(o.idleTap);  // nobody is talking: he just chirps (HAL Paper refreshes his page)
  TEST_ASSERT_EQUAL(0, int(o.send.size()));
  nano.onMessage(msg("NOW 1 zero Something long-winded."), 0);
  o = nano.onTap();
  TEST_ASSERT_EQUAL_STRING("END interrupted", sent(o).c_str());  // a tap on any HAL stops them all
  TEST_ASSERT_TRUE(o.stop);
  TEST_ASSERT_FALSE(o.idleTap);
  TEST_ASSERT_FALSE(nano.talking());
  TEST_ASSERT_TRUE(nano.onTap().idleTap);
}

void test_friend_end_and_hang_up() {
  Friend card(hal::kCard);
  card.onMessage(msg(std::string("FETCH 1 192.168.1.20 9100 ") + kToken), 0);
  card.onFetched(1, true);
  card.onMessage(msg("SAY live 1"), 100);
  card.onPlaying(1, 5000, true);
  FriendOut o = card.onMessage(msg("END interrupted"), 200);
  TEST_ASSERT_TRUE(o.stop);  // stop speaking, forget the clip
  TEST_ASSERT_FALSE(card.speaking());
  TEST_ASSERT_FALSE(card.talking());
  TEST_ASSERT_EQUAL(0, int(card.onSaid().send.size()));  // no DONE for a line that was cut off
  TEST_ASSERT_EQUAL_STRING("MISSING 1", sent(card.onMessage(msg("SAY live 1"), 300)).c_str());
  // END done at the natural end also clears the stage, without anything to stop mid-word.
  card.onMessage(msg("NOW 1 zero Bye."), 400);
  o = card.onMessage(msg("END done"), 500);
  TEST_ASSERT_TRUE(o.stop);
  // HAL Zero vanishing does the same.
  card.onMessage(msg("NOW 1 zero Hello again."), 600);
  o = card.onDisconnected();
  TEST_ASSERT_TRUE(o.stop);
  TEST_ASSERT_FALSE(card.talking());
}

void test_friend_stops_expecting_a_conversation_that_went_quiet() {
  Friend nano(hal::kNano);
  nano.onMessage(msg("NOW 1 zero Hello."), 1000);
  nano.onMessage(msg("PING"), 30000);  // housekeeping doesn't count
  TEST_ASSERT_TRUE(nano.tick(61000).send.empty());
  TEST_ASSERT_TRUE(nano.talking());
  FriendOut o = nano.tick(61001);       // a minute with no word of the conversation
  TEST_ASSERT_TRUE(o.stop);
  TEST_ASSERT_FALSE(nano.talking());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_chirps_follow_the_words);
  RUN_TEST(test_chirp_time);
  RUN_TEST(test_reading_time);
  RUN_TEST(test_minutes_wrap_and_stack);
  RUN_TEST(test_minutes_turn_the_page_when_full);
  RUN_TEST(test_minutes_clear_starts_a_fresh_page);
  RUN_TEST(test_friend_says_who_he_is);
  RUN_TEST(test_friend_housekeeping);
  RUN_TEST(test_friend_follows_who_is_speaking);
  RUN_TEST(test_friend_with_no_voice_says_his_line_from_the_words);
  RUN_TEST(test_friend_wont_say_what_he_was_never_given);
  RUN_TEST(test_friend_one_line_at_a_time);
  RUN_TEST(test_friend_with_a_voice_fetches_then_speaks);
  RUN_TEST(test_friend_with_a_voice_when_things_go_wrong);
  RUN_TEST(test_friend_a_newer_fetch_replaces_the_one_in_hand);
  RUN_TEST(test_friend_tap);
  RUN_TEST(test_friend_end_and_hang_up);
  RUN_TEST(test_friend_stops_expecting_a_conversation_that_went_quiet);
  return UNITY_END();
}

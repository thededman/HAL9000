#include <unity.h>

#include <string>
#include <vector>

#include "hal_facts.h"
#include "hal_http.h"
#include "hal_keymap.h"
#include "hal_textbox.h"

using hal::BrainReply;
using hal::HttpResponse;

void setUp() {}
void tearDown() {}

namespace {

const char* const kKey = "0123456789abcdef0123456789abcdef";
const char* const kClip = "00112233445566778899aabbccddeeff";

BrainReply reply(int status, const std::string& body) {
  HttpResponse r;
  r.status = status;
  r.body = body;
  return hal::parseBrainReply(r);
}

std::string response(const std::string& statusLine, const std::string& headers, const std::string& body) {
  return statusLine + "\r\n" + headers + "\r\n" + body;
}

}  // namespace

// ---- the text box ----

void test_textbox_types_and_erases() {
  hal::TextBox box;
  TEST_ASSERT_TRUE(box.empty());
  for (char c : std::string("Hi, HAL?")) TEST_ASSERT_TRUE(box.insert(c));
  TEST_ASSERT_EQUAL_STRING("Hi, HAL?", box.text().c_str());
  TEST_ASSERT_TRUE(box.backspace());
  TEST_ASSERT_EQUAL_STRING("Hi, HAL", box.text().c_str());
  box.clear();
  TEST_ASSERT_TRUE(box.empty());
  TEST_ASSERT_FALSE(box.backspace());  // nothing to erase: no harm
}

void test_textbox_stops_at_120() {
  hal::TextBox box;
  for (int i = 0; i < 120; ++i) TEST_ASSERT_TRUE(box.insert('a'));
  TEST_ASSERT_TRUE(box.full());
  TEST_ASSERT_FALSE(box.insert('b'));  // the 121st
  TEST_ASSERT_EQUAL(120, int(box.text().size()));
  TEST_ASSERT_TRUE(box.backspace());
  TEST_ASSERT_TRUE(box.insert('c'));
  hal::TextBox tiny(3);
  tiny.insert('a'); tiny.insert('b'); tiny.insert('c');
  TEST_ASSERT_FALSE(tiny.insert('d'));
}

void test_textbox_refuses_unprintable_and_leading_space() {
  hal::TextBox box;
  for (char c : {'\n', '\t', '\0', '\x7f', '\x1b', static_cast<char>(0xE9)}) TEST_ASSERT_FALSE(box.insert(c));
  TEST_ASSERT_FALSE(box.insert(' '));  // a question doesn't start with a space
  TEST_ASSERT_TRUE(box.insert('a'));
  TEST_ASSERT_TRUE(box.insert(' '));
  TEST_ASSERT_TRUE(box.insert('~'));
  TEST_ASSERT_EQUAL_STRING("a ~", box.text().c_str());
}

// ---- the key map ----

void test_keymap_letters_digits_space() {
  TEST_ASSERT_EQUAL_CHAR('a', hal::keyToChar(30, false));
  TEST_ASSERT_EQUAL_CHAR('A', hal::keyToChar(30, true));
  TEST_ASSERT_EQUAL_CHAR('q', hal::keyToChar(16, false));
  TEST_ASSERT_EQUAL_CHAR('p', hal::keyToChar(25, false));
  TEST_ASSERT_EQUAL_CHAR('s', hal::keyToChar(31, false));  // the code the Cardputer logged for S
  TEST_ASSERT_EQUAL_CHAR('m', hal::keyToChar(50, false));
  TEST_ASSERT_EQUAL_CHAR('Z', hal::keyToChar(44, true));
  TEST_ASSERT_EQUAL_CHAR('1', hal::keyToChar(2, false));
  TEST_ASSERT_EQUAL_CHAR('!', hal::keyToChar(2, true));
  TEST_ASSERT_EQUAL_CHAR('0', hal::keyToChar(11, false));
  TEST_ASSERT_EQUAL_CHAR(' ', hal::keyToChar(57, false));
  TEST_ASSERT_EQUAL_CHAR(' ', hal::keyToChar(57, true));
  int letters = 0;
  for (int code = 0; code < 128; ++code) {
    char c = hal::keyToChar(code, false);
    if (c >= 'a' && c <= 'z') ++letters;
    if (c) TEST_ASSERT_TRUE(c >= 32 && c <= 126 && hal::keyToChar(code, true) >= 32);
  }
  TEST_ASSERT_EQUAL(26, letters);
}

void test_keymap_punctuation() {
  TEST_ASSERT_EQUAL_CHAR('/', hal::keyToChar(53, false));
  TEST_ASSERT_EQUAL_CHAR('?', hal::keyToChar(53, true));
  TEST_ASSERT_EQUAL_CHAR('\'', hal::keyToChar(40, false));
  TEST_ASSERT_EQUAL_CHAR('"', hal::keyToChar(40, true));
  TEST_ASSERT_EQUAL_CHAR(',', hal::keyToChar(51, false));
  TEST_ASSERT_EQUAL_CHAR('.', hal::keyToChar(52, false));
  TEST_ASSERT_EQUAL_CHAR('-', hal::keyToChar(12, false));
}

void test_keymap_unknown_is_zero() {
  for (int code : {389, 383, 28, 14, 1, 15, 42, 54, 29, 56, 0, -1, 9999}) {  // Fn, Sym, Enter, Backspace, Esc, Tab, Shifts, Ctrl, Alt
    TEST_ASSERT_EQUAL_CHAR(0, hal::keyToChar(code, false));
    TEST_ASSERT_EQUAL_CHAR(0, hal::keyToChar(code, true));
  }
}

// ---- requests ----

void test_http_request() {
  TEST_ASSERT_EQUAL_STRING(
      "POST /ask?who=zero HTTP/1.0\r\nHost: 192.168.1.20:9100\r\nX-HAL-Key: 0123456789abcdef0123456789abcdef\r\n"
      "Content-Type: text/plain\r\nContent-Length: 7\r\nConnection: close\r\n\r\n\nHello?",
      hal::httpRequest("POST", "192.168.1.20", 9100, "/ask?who=zero", kKey, "text/plain", "\nHello?").c_str());
  TEST_ASSERT_EQUAL_STRING("GET /clip/abc HTTP/1.0\r\nHost: brain.local:9100\r\nConnection: close\r\n\r\n",
                           hal::httpRequest("GET", "brain.local", 9100, "/clip/abc", "", "", "ignored").c_str());
  TEST_ASSERT_EQUAL_STRING("POST /debate/00/stop HTTP/1.0\r\nHost: h:1\r\nContent-Length: 0\r\nConnection: close\r\n\r\n",
                           hal::httpRequest("POST", "h", 1, "/debate/00/stop", "", "", "").c_str());
  std::string audio("RIFF\0\0\r\n\r\n", 10);  // a body may hold anything
  std::string r = hal::httpRequest("POST", "h", 9100, "/hear", kKey, "audio/wav", audio);
  TEST_ASSERT_TRUE(r.size() > 10 && r.compare(r.size() - 10, 10, audio) == 0);
  TEST_ASSERT_TRUE(r.find("Content-Length: 10\r\n") != std::string::npos);
}

void test_http_request_refuses_injection() {
  const char* h = "192.168.1.20";
  TEST_ASSERT_EQUAL_STRING("", hal::httpRequest("POST", "a b", 9100, "/ask", kKey, "", "x").c_str());
  TEST_ASSERT_EQUAL_STRING("", hal::httpRequest("POST", "a\r\nX: y", 9100, "/ask", kKey, "", "x").c_str());
  TEST_ASSERT_EQUAL_STRING("", hal::httpRequest("POST", h, 9100, "/ask HTTP/1.0\r\nX: y", kKey, "", "x").c_str());
  TEST_ASSERT_EQUAL_STRING("", hal::httpRequest("POST", h, 9100, "ask", kKey, "", "x").c_str());
  TEST_ASSERT_EQUAL_STRING("", hal::httpRequest("POST", h, 9100, "/ask", "key\r\nX: y", "", "x").c_str());
  TEST_ASSERT_EQUAL_STRING("", hal::httpRequest("POST", h, 9100, "/ask", kKey, "text/plain\r\nX: y", "x").c_str());
  TEST_ASSERT_EQUAL_STRING("", hal::httpRequest("DELETE", h, 9100, "/ask", kKey, "", "x").c_str());
  TEST_ASSERT_EQUAL_STRING("", hal::httpRequest("POST", h, 0, "/ask", kKey, "", "x").c_str());
  TEST_ASSERT_EQUAL_STRING("", hal::httpRequest("POST", h, 70000, "/ask", kKey, "", "x").c_str());
  TEST_ASSERT_EQUAL_STRING("", hal::httpRequest("POST", "", 9100, "/ask", kKey, "", "x").c_str());
}

// ---- responses ----

void test_http_response() {
  HttpResponse r;
  TEST_ASSERT_TRUE(hal::parseHttpResponse(
      response("HTTP/1.0 200 OK", "Server: HALBrain\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: 5\r\n", "ok=1\n"), &r));
  TEST_ASSERT_EQUAL(200, r.status);
  TEST_ASSERT_EQUAL_STRING("text/plain; charset=utf-8", r.contentType.c_str());
  TEST_ASSERT_EQUAL_STRING("ok=1\n", r.body.c_str());
  TEST_ASSERT_TRUE(hal::parseHttpResponse(response("HTTP/1.1 204 No Content", "content-length: 0\r\n", ""), &r));
  TEST_ASSERT_EQUAL(204, r.status);
  TEST_ASSERT_EQUAL_STRING("", r.body.c_str());
  TEST_ASSERT_TRUE(hal::parseHttpResponse(response("HTTP/1.0 200 OK", "Content-Length: 4\r\n", "ok=1 and more"), &r));
  TEST_ASSERT_EQUAL_STRING("ok=1", r.body.c_str());  // no more than it said
  TEST_ASSERT_TRUE(hal::parseHttpResponse(response("HTTP/1.0 503", "", "error=no-model\n"), &r));  // no length: to the end
  TEST_ASSERT_EQUAL(503, r.status);
  TEST_ASSERT_EQUAL_STRING("error=no-model\n", r.body.c_str());
  std::string wav("RIFF\0\0\0\0WAVE", 12);  // bodies aren't text
  TEST_ASSERT_TRUE(hal::parseHttpResponse(response("HTTP/1.0 200 OK", "Content-Type: audio/wav\r\nContent-Length: 12\r\n", wav), &r));
  TEST_ASSERT_EQUAL(12, int(r.body.size()));
}

void test_http_response_rejects() {
  HttpResponse r;
  const std::string bad[] = {
      "",
      "HTTP/1.0 200 OK\r\nContent-Length: 5\r\n",                        // no blank line
      response("HTTP/1.0 200 OK", "Content-Length: 50\r\n", "ok=1\n"),   // cut short
      response("HTTP/1.0 099 Odd", "", ""),
      response("HTTP/1.0 600 Odd", "", ""),
      response("HTTP/1.0 2x0 OK", "", ""),
      response("HTTP/1.0 2000 OK", "", ""),
      response("HTTP/2 200 OK", "", ""),
      response("SSH-2.0-OpenSSH", "", ""),
      response("HTTP/1.0 200 OK", "Content-Length: lots\r\n", "ok=1\n"),
      response("HTTP/1.0 200 OK", "Content-Length: 99999999\r\n", "ok=1\n"),
  };
  for (const std::string& raw : bad) {
    TEST_ASSERT_FALSE_MESSAGE(hal::parseHttpResponse(raw, &r), raw.c_str());
    TEST_ASSERT_EQUAL(0, r.status);
  }
}

// The desk HAL downloads a clip a piece at a time: he needs the head before the body is all there.
void test_http_head() {
  int status = 0;
  long length = 0;
  TEST_ASSERT_TRUE(hal::parseHttpHead("HTTP/1.0 200 OK\r\nServer: HALBrain\r\nContent-Type: audio/wav\r\n"
                                      "Content-Length: 133584\r\nConnection: close\r\n\r\n", &status, &length));
  TEST_ASSERT_EQUAL(200, status);
  TEST_ASSERT_EQUAL(133584, length);
  TEST_ASSERT_TRUE(hal::parseHttpHead("HTTP/1.1 404 Not Found\r\ncontent-length: 16\r\n\r\n", &status, &length));
  TEST_ASSERT_EQUAL(404, status);
  TEST_ASSERT_EQUAL(16, length);
  TEST_ASSERT_TRUE(hal::parseHttpHead("HTTP/1.0 200 OK\r\n\r\n", &status, &length));
  TEST_ASSERT_EQUAL(-1, length);  // no length given
  const char* const bad[] = {"", "HTTP/1.0 200 OK\r\nContent-Length: 5\r\n",  // no blank line yet
                             "HTTP/1.0 2x0 OK\r\n\r\n", "SSH-2.0-OpenSSH\r\n\r\n", "HTTP/1.0 200 OK\r\nContent-Length: lots\r\n\r\n",
                             "HTTP/1.0 200 OK\r\nContent-Length: 99999999999\r\n\r\n", "HTTP/1.0 200 OK\r\nContent-Length: -5\r\n\r\n"};
  for (const char* head : bad) TEST_ASSERT_FALSE_MESSAGE(hal::parseHttpHead(head, &status, &length), head);
}

// ---- the brain's replies ----

void test_reply_ok() {
  BrainReply a = reply(200, std::string("text=I am a cool 47 C.\nclip=") + kClip + "\nms=3028\n");
  TEST_ASSERT_TRUE(a.ok);
  TEST_ASSERT_EQUAL_STRING("I am a cool 47 C.", a.text.c_str());
  TEST_ASSERT_EQUAL_STRING(kClip, a.clip.c_str());
  TEST_ASSERT_EQUAL(3028, a.ms);
  TEST_ASSERT_EQUAL_STRING("", a.show.c_str());
  BrainReply s = reply(200, std::string("text=Look.\r\nclip=") + kClip + "\r\nms=900\r\nshow=wall\r\nfuture=ignored\r\n");
  TEST_ASSERT_TRUE(s.ok);
  TEST_ASSERT_EQUAL_STRING("wall", s.show.c_str());
  BrainReply h = reply(200, "heard=What time is it?\n");
  TEST_ASSERT_TRUE(h.ok && h.hasHeard);
  TEST_ASSERT_EQUAL_STRING("What time is it?", h.heard.c_str());
  BrainReply nothing = reply(200, "heard=\n");
  TEST_ASSERT_TRUE(nothing.ok && nothing.hasHeard);
  TEST_ASSERT_EQUAL_STRING("", nothing.heard.c_str());
  BrainReply d = reply(200, "debate=0123456789abcdef\n");
  TEST_ASSERT_TRUE(d.ok);
  TEST_ASSERT_EQUAL_STRING("0123456789abcdef", d.debate.c_str());
  BrainReply l = reply(200, std::string("who=desk\ntext=Last word.\nclip=") + kClip + "\nms=1500\nlast=1\n");
  TEST_ASSERT_TRUE(l.ok && l.last);
  TEST_ASSERT_EQUAL_STRING("desk", l.who.c_str());
  TEST_ASSERT_TRUE(reply(200, "ok=1\n").ok);
}

void test_reply_heard_is_made_safe() {
  BrainReply h = reply(200, "heard=caf\xC3\xA9 \x1b[31mred" + std::string(200, 'x') + "\n");
  TEST_ASSERT_TRUE(h.ok);
  TEST_ASSERT_EQUAL(120, int(h.heard.size()));           // what the text box holds
  TEST_ASSERT_TRUE(h.heard.compare(0, 13, "caf [31mredxx") == 0);  // no escape or accent bytes
}

void test_reply_wait_and_end() {
  BrainReply w = reply(202, "wait=1\n");
  TEST_ASSERT_TRUE(w.ok && w.wait && !w.end);
  BrainReply e = reply(204, "");
  TEST_ASSERT_TRUE(e.ok && e.end && !e.wait);
}

void test_reply_errors() {
  BrainReply k = reply(401, "error=bad-key\n");
  TEST_ASSERT_FALSE(k.ok);
  TEST_ASSERT_EQUAL_STRING("bad-key", k.error.c_str());
  TEST_ASSERT_EQUAL_STRING("no-model", reply(503, "error=no-model\n").error.c_str());
  TEST_ASSERT_EQUAL_STRING("http-503", reply(503, "").error.c_str());
  TEST_ASSERT_EQUAL_STRING("http-500", reply(500, "<html>Internal error</html>").error.c_str());
  TEST_ASSERT_EQUAL_STRING("http-404", reply(404, "error=Not Found!\n").error.c_str());  // not one of ours
  TEST_ASSERT_EQUAL_STRING("", reply(401, "error=bad-key\ntext=sneaky\n").text.c_str());  // an error carries nothing else
}

void test_reply_rejects() {
  const std::string clip = std::string("\nclip=") + kClip + "\nms=100\n";
  const std::string bad[] = {
      "",                                                    // nothing
      "hello\n",                                             // nothing we know
      "text=No clip.\nms=100\n",                             // text without its audio
      std::string("clip=") + kClip + "\nms=100\n",           // audio without text
      std::string("text=No length.\nclip=") + kClip + "\n",
      "text=" + clip.substr(1),                              // empty text
      "text=" + std::string(151, 'a') + clip,                // too long for a subtitle
      "text=caf\xC3\xA9" + clip,                             // not plain ASCII
      "text=bell\x07" + clip,
      "text=Hi.\nclip=0011223344556677\nms=100\n",           // a short token
      "text=Hi.\nclip=00112233445566778899AABBCCDDEEFF\nms=100\n",
      "text=Hi.\nclip=../../../../etc/passwd\nms=100\n",
      std::string("text=Hi.\nclip=") + kClip + "\nms=9999999\n",
      std::string("text=Hi.\nclip=") + kClip + "\nms=-5\n",
      std::string("text=Hi.\nclip=") + kClip + "\nms=soon\n",
      "text=Hi." + clip + "show=Wall\n",
      "text=Hi." + clip + "show=../x\n",
      "text=Hi." + clip + "show=\n",
      "text=Hi." + clip + "show=aaaaaaaaaaaaaaaaa\n",        // 17 letters
      "text=Hi." + clip + "who=dave\n",
      "who=zero\n",
      "debate=0123\n",
      "debate=0123456789ABCDEF\n",
      "text=Hi." + clip + "last=2\n",
      "text=Hi." + clip + std::string(3000, 'x'),            // far more than any reply
  };
  for (const std::string& body : bad) {
    BrainReply r = reply(200, body);
    TEST_ASSERT_FALSE_MESSAGE(r.ok, body.substr(0, 60).c_str());
    TEST_ASSERT_EQUAL_STRING("bad-reply", r.error.c_str());
    TEST_ASSERT_EQUAL_STRING("", r.text.c_str());  // nothing of a bad reply is kept
    TEST_ASSERT_EQUAL_STRING("", r.clip.c_str());
  }
}

void test_ask_body() {
  TEST_ASSERT_EQUAL_STRING("time: 16:42\ntemp: 47 C\n\nHow hot?", hal::askBody("time: 16:42\ntemp: 47 C", "How hot?").c_str());
  TEST_ASSERT_EQUAL_STRING("time: 16:42\n\nHow hot?", hal::askBody("time: 16:42\n\n", "How hot?").c_str());
  TEST_ASSERT_EQUAL_STRING("\nHow hot?", hal::askBody("", "How hot?").c_str());
}

// ---- facts ----

void test_facts_from_readings() {
  std::vector<hal::Reading> rs = {hal::cpuReading(8), hal::memoryReading(126976, 360448), hal::tempReading(46),
                                  hal::netReading(-42), hal::linkReading(true, true), hal::tiltReading(-1),
                                  hal::clockReading(21 * 3600 + 4 * 60), hal::peerReading(hal::PeerState::Ok, "idle"),
                                  hal::Reading()};
  TEST_ASSERT_EQUAL_STRING("processor load: 8 %\nfree memory: 124 MB\nchip temperature: 46 C\nwifi signal: -42 DBM\n"
                           "link to the other HAL: CONNECTED\ntime: 21:04\nthe desk HAL: IDLE",
                           hal::factsFromReadings(rs).c_str());
  TEST_ASSERT_EQUAL_STRING("", hal::factsFromReadings({}).c_str());
  hal::Reading custom = hal::noReading("XYZ", "SPARE PART");
  custom.valid = true;
  custom.value = "7";
  TEST_ASSERT_EQUAL_STRING("spare part: 7", hal::factsFromReadings({custom}).c_str());
}

void test_desk_facts() {
  hal::PeerStatus st;
  st.upS = 3900; st.heap = 99328; st.psram = 7340032; st.cam = 12; st.lines = 16; st.mode = "idle"; st.rssi = -47;
  st.fw = "0.6.0"; st.cores = 2; st.mhz = 240;
  hal::PeerSensors se = {67, 19, 3, 37, true, 42};
  TEST_ASSERT_EQUAL_STRING("uptime: 1 h 5 min\nfree memory: 97 KB\nlines spoken: 16\nmode: idle\nwifi signal: -47 dBm\n"
                           "firmware: 0.6.0\ncamera motion: 12 %\nroom light: 67 %\nbattery: 37 % and charging\n"
                           "tilt: 3 degrees\nchip temperature: 42 C\nproximity: 19",
                           hal::deskFacts(&st, &se).c_str());
  hal::PeerSensors unread = {-1, 0, 0, -1, false, 30};
  TEST_ASSERT_EQUAL_STRING("tilt: 0 degrees\nchip temperature: 30 C\nproximity: 0", hal::deskFacts(nullptr, &unread).c_str());
  st.rssi = 0;
  TEST_ASSERT_TRUE(hal::deskFacts(&st, nullptr).find("wifi signal: off") != std::string::npos);
  TEST_ASSERT_EQUAL_STRING("", hal::deskFacts(nullptr, nullptr).c_str());
}

// An argument gets both HALs' readings, each line saying whose it is.
void test_debate_facts() {
  std::vector<hal::Reading> rs = {hal::cpuReading(8), hal::memoryReading(134144, 360448), hal::tempReading(54),
                                  hal::netReading(-42), hal::linkReading(true, true), hal::cameraReading(true, 3),
                                  hal::tiltReading(2), hal::audioReading(0, 4), hal::clockReading(20 * 3600 + 24 * 60),
                                  hal::uptimeReading(600), hal::peerReading(hal::PeerState::Ok, "idle")};
  hal::PeerStatus st;
  st.upS = 3900; st.heap = 99328; st.cam = 12; st.lines = 16; st.mode = "idle"; st.rssi = -47; st.fw = "0.6.0";
  hal::PeerSensors se = {67, 19, 3, 53, true, 42};
  TEST_ASSERT_EQUAL_STRING(
      "time: 20:24\n"
      "zero processor load: 8 %\nzero free memory: 131 MB\nzero chip temperature: 54 C\nzero wifi signal: -42 DBM\n"
      "zero lines spoken: 4 LINES\nzero uptime: 00:10:00\n"
      "desk uptime: 1 h 5 min\ndesk free memory: 97 KB\ndesk lines spoken: 16\ndesk mode: idle\ndesk wifi signal: -47 dBm\n"
      "desk firmware: 0.6.0\ndesk room light: 67 %\ndesk battery: 53 % and charging\ndesk tilt: 3 degrees\n"
      "desk chip temperature: 42 C",
      hal::debateFacts(rs, &st, &se).c_str());
  // Without anything from the desk HAL, only HAL Zero's; and never more than the brain accepts (24 lines).
  std::string alone = hal::debateFacts(rs, nullptr, nullptr);
  TEST_ASSERT_TRUE(alone.find("desk ") == std::string::npos);
  int lines = 1;
  for (char c : hal::debateFacts(rs, &st, &se)) lines += c == '\n';
  TEST_ASSERT_TRUE(lines <= 24);
  TEST_ASSERT_EQUAL_STRING("", hal::debateFacts({}, nullptr, nullptr).c_str());
}

// ----- the cast (v0.7) -----

void test_reply_for_a_hal_with_no_voice() {
  // HAL Nano chirps and HAL Paper writes: their lines come as words, with no audio to fetch.
  hal::BrainReply r = reply(200, "who=nano\ntext=Is it my turn?\nlast=0\n");
  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_EQUAL_STRING("nano", r.who.c_str());
  TEST_ASSERT_EQUAL_STRING("Is it my turn?", r.text.c_str());
  TEST_ASSERT_EQUAL_STRING("", r.clip.c_str());
  r = reply(200, "who=paper\ntext=Noted.\nms=0\nlast=1\n");
  TEST_ASSERT_TRUE(r.ok && r.last);
  // HAL Card has a voice, like the two before him.
  r = reply(200, std::string("who=card\ntext=I was first.\nclip=") + kClip + "\nms=4100\n");
  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_EQUAL_STRING("card", r.who.c_str());
  TEST_ASSERT_EQUAL(4100, r.ms);
}

void test_reply_cast_rejects() {
  const std::string bad[] = {
      "who=card\ntext=No audio for a HAL who speaks.\n",
      "who=desk\ntext=Nor for him.\nms=100\n",
      std::string("who=nano\ntext=Audio for a HAL who can't play it.\nclip=") + kClip + "\nms=100\n",
      "who=nano\n",                       // a speaker and nothing said
      "who=wristwatch\ntext=Hello.\n",   // nobody we know
      "text=Words, from nobody in particular.\n",
  };
  for (const std::string& body : bad) {
    hal::BrainReply r = reply(200, body);
    TEST_ASSERT_FALSE_MESSAGE(r.ok, body.c_str());
    TEST_ASSERT_EQUAL_STRING("bad-reply", r.error.c_str());
  }
}

static int countLines(const std::string& s) {
  if (s.empty()) return 0;
  int n = 1;
  for (char c : s) n += c == '\n';
  return n;
}

static int linesOf(const std::string& s, const std::string& owner) {
  int n = 0;
  size_t at = 0;
  while (at < s.size()) {
    if (s.compare(at, owner.size() + 1, owner + " ") == 0) ++n;
    at = s.find('\n', at);
    if (at == std::string::npos) break;
    ++at;
  }
  return n;
}

void test_cast_facts_for_five() {
  std::vector<hal::Reading> rs = {hal::cpuReading(8), hal::memoryReading(134144, 360448), hal::tempReading(54),
                                  hal::netReading(-42), hal::linkReading(true, true), hal::cameraReading(true, 3),
                                  hal::tiltReading(2), hal::audioReading(0, 4), hal::clockReading(20 * 3600 + 24 * 60),
                                  hal::uptimeReading(600), hal::peerReading(hal::PeerState::Ok, "idle")};
  hal::PeerStatus desk, nano, paper, card;
  desk.upS = 3900; desk.heap = 99328; desk.cam = 12; desk.lines = 16; desk.mode = "idle"; desk.rssi = -47; desk.fw = "0.7.0";
  nano = paper = card = desk;
  nano.upS = 120; paper.upS = 86400; card.upS = 7300;
  hal::PeerSensors deskS = {67, 19, 3, 53, true, 42};
  hal::PeerSensors nanoS = {-1, 0, 38, 81, false, 44};   // no light sensor; tilted on a wrist
  hal::PeerSensors paperS = {-1, 0, 0, 64, false, 22};   // 22 C is the room, from his own sensor
  hal::PeerSensors cardS = {-1, 0, 0, 55, true, 48};
  const std::string facts = hal::castFacts(rs, {{hal::kDesk, &desk, &deskS}, {hal::kNano, &nano, &nanoS},
                                                 {hal::kPaper, &paper, &paperS}, {hal::kCard, &card, &cardS}});
  TEST_ASSERT_EQUAL_STRING("time: 20:24", facts.substr(0, facts.find('\n')).c_str());
  TEST_ASSERT_TRUE_MESSAGE(countLines(facts) <= 24, facts.c_str());  // what the brain accepts
  for (const char* who : {"zero", "desk", "nano", "paper", "card"})
    TEST_ASSERT_TRUE_MESSAGE(linesOf(facts, who) >= 3, who);  // nobody is squeezed out, the last least of all
  // Each says what he can really tell.
  TEST_ASSERT_TRUE(facts.find("nano battery: 81 %") != std::string::npos);
  TEST_ASSERT_TRUE(facts.find("nano tilt: 38 degrees") != std::string::npos);
  TEST_ASSERT_TRUE(facts.find("paper battery: 64 %") != std::string::npos);
  TEST_ASSERT_TRUE(facts.find("paper room temperature: 22 C") != std::string::npos);
  TEST_ASSERT_TRUE(facts.find("card battery: 55 % and charging") != std::string::npos);
  TEST_ASSERT_TRUE(facts.find("card chip temperature: 48 C") != std::string::npos);
  TEST_ASSERT_TRUE(facts.find("desk room light: 67 %") != std::string::npos);
  TEST_ASSERT_TRUE(facts.find("zero chip temperature: 54 C") != std::string::npos);
  // ... and nothing his device has no sensor for.
  for (const char* nonsense : {"paper tilt", "card tilt", "nano room light", "paper room light", "card room light",
                               "proximity", "camera motion", "paper chip temperature"})
    TEST_ASSERT_TRUE_MESSAGE(facts.find(nonsense) == std::string::npos, nonsense);
}

void test_cast_facts_share_a_small_budget() {
  std::vector<hal::Reading> rs = {hal::tempReading(54), hal::memoryReading(134144, 360448), hal::uptimeReading(600),
                                  hal::clockReading(9 * 3600)};
  hal::PeerStatus st;
  st.upS = 60; st.mode = "idle"; st.fw = "0.7.0";
  hal::PeerSensors se = {50, 0, 0, 90, false, 40};
  const std::string facts = hal::castFacts(rs, {{hal::kDesk, &st, &se}, {hal::kNano, &st, &se}, {hal::kCard, &st, &se}}, 9);
  TEST_ASSERT_TRUE(countLines(facts) <= 9);
  for (const char* who : {"zero", "desk", "nano", "card"}) TEST_ASSERT_EQUAL_MESSAGE(2, linesOf(facts, who), who);
}

void test_cast_facts_with_gaps() {
  std::vector<hal::Reading> rs = {hal::tempReading(54), hal::clockReading(9 * 3600)};
  hal::PeerSensors se = {-1, 0, 0, 90, false, 40};
  // A HAL who hasn't answered yet has nothing said about him; one with sensors only has those.
  const std::string facts = hal::castFacts(rs, {{hal::kDesk, nullptr, nullptr}, {hal::kCard, nullptr, &se}, {9, nullptr, &se}});
  TEST_ASSERT_EQUAL(0, linesOf(facts, "desk"));
  TEST_ASSERT_TRUE(linesOf(facts, "card") >= 2);
  TEST_ASSERT_TRUE(facts.find("zero chip temperature: 54 C") != std::string::npos);
  TEST_ASSERT_EQUAL_STRING("", hal::castFacts({}, {}).c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_textbox_types_and_erases);
  RUN_TEST(test_textbox_stops_at_120);
  RUN_TEST(test_textbox_refuses_unprintable_and_leading_space);
  RUN_TEST(test_keymap_letters_digits_space);
  RUN_TEST(test_keymap_punctuation);
  RUN_TEST(test_keymap_unknown_is_zero);
  RUN_TEST(test_http_request);
  RUN_TEST(test_http_request_refuses_injection);
  RUN_TEST(test_http_response);
  RUN_TEST(test_http_response_rejects);
  RUN_TEST(test_http_head);
  RUN_TEST(test_reply_ok);
  RUN_TEST(test_reply_heard_is_made_safe);
  RUN_TEST(test_reply_wait_and_end);
  RUN_TEST(test_reply_errors);
  RUN_TEST(test_reply_rejects);
  RUN_TEST(test_ask_body);
  RUN_TEST(test_facts_from_readings);
  RUN_TEST(test_desk_facts);
  RUN_TEST(test_debate_facts);
  RUN_TEST(test_reply_for_a_hal_with_no_voice);
  RUN_TEST(test_reply_cast_rejects);
  RUN_TEST(test_cast_facts_for_five);
  RUN_TEST(test_cast_facts_share_a_small_budget);
  RUN_TEST(test_cast_facts_with_gaps);
  return UNITY_END();
}

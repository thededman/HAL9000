#pragma once
// The text HAL Zero exchanges with the AI brain: an HTTP/1.0 request, and the brain's
// key=value reply checked field by field before anything is shown, fetched or said.
// (Sockets are the glue's business; this is only the text.)
#include <string>

namespace hal {

// A whole request, ready to send. `key` goes in X-HAL-Key when it isn't empty;
// Content-Length is always sent on POST. Empty string if anything could break out
// of its line (a space, CR, LF or other control character in host, path, key or type).
std::string httpRequest(const std::string& method, const std::string& host, int port, const std::string& path,
                        const std::string& key, const std::string& contentType, const std::string& body);

struct HttpResponse {
  int status = 0;
  std::string contentType;
  std::string body;
};

// A whole response (the connection closed). False if it isn't one, or the body is
// shorter than its Content-Length says.
bool parseHttpResponse(const std::string& raw, HttpResponse* out);

struct BrainReply {
  bool ok = false;      // the brain answered and every field is what the spec promises
  std::string error;    // when !ok: the brain's error code, "http-<status>" or "bad-reply"
  std::string text;     // what the HAL says: 1..150 printable ASCII characters
  std::string clip;     // its audio: 32 hex characters
  int ms = 0;           // how long the audio plays, 0..600000
  std::string show;     // a screen to bring up, or empty
  std::string heard;    // POST /hear: what was said (may be empty)
  bool hasHeard = false;
  std::string who;      // a debate line's speaker: "zero" or "desk"
  std::string debate;   // POST /debate: its id, 16 hex characters
  bool last = false;    // this debate line is the final one
  bool wait = false;    // 202: not ready yet, ask again
  bool end = false;     // 204: past the last line
};

BrainReply parseBrainReply(const HttpResponse& response);

// The body of POST /ask and POST /debate: facts, a blank line, the question.
std::string askBody(const std::string& facts, const std::string& question);

}  // namespace hal

#include "hal_http.h"

#include "hal_cast.h"

#include <cctype>
#include <map>

namespace hal {
namespace {

bool oneToken(const std::string& s) {  // no space, no control characters, not empty
  if (s.empty()) return false;
  for (unsigned char c : s)
    if (c <= 32 || c >= 127) return false;
  return true;
}

bool isHex(const std::string& s, size_t length) {
  if (s.size() != length) return false;
  for (char c : s)
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  return true;
}

bool printable(const std::string& s, size_t min, size_t max) {
  if (s.size() < min || s.size() > max) return false;
  for (unsigned char c : s)
    if (c < 32 || c > 126) return false;
  return true;
}

bool smallNumber(const std::string& s, int max, int* out) {
  if (s.empty() || s.size() > 6) return false;
  int v = 0;
  for (char c : s) {
    if (c < '0' || c > '9') return false;
    v = v * 10 + (c - '0');
  }
  if (v > max) return false;
  *out = v;
  return true;
}

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

// key=value lines; the first of a repeated key wins; lines without '=' are skipped.
std::map<std::string, std::string> fieldsOf(const std::string& body) {
  std::map<std::string, std::string> f;
  size_t start = 0;
  while (start < body.size()) {
    size_t end = body.find('\n', start);
    if (end == std::string::npos) end = body.size();
    std::string line = body.substr(start, end - start);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    size_t eq = line.find('=');
    if (eq != std::string::npos && eq > 0) f.emplace(line.substr(0, eq), line.substr(eq + 1));
    start = end + 1;
  }
  return f;
}

}  // namespace

std::string httpRequest(const std::string& method, const std::string& host, int port, const std::string& path,
                        const std::string& key, const std::string& contentType, const std::string& body) {
  const bool post = method == "POST";
  if ((!post && method != "GET") || !oneToken(host) || !oneToken(path) || path[0] != '/' || port < 1 || port > 65535)
    return "";
  if ((!key.empty() && !oneToken(key)) || (!contentType.empty() && !oneToken(contentType))) return "";
  std::string r = method + " " + path + " HTTP/1.0\r\nHost: " + host + ":" + std::to_string(port) + "\r\n";
  if (!key.empty()) r += "X-HAL-Key: " + key + "\r\n";
  if (!contentType.empty()) r += "Content-Type: " + contentType + "\r\n";
  if (post) r += "Content-Length: " + std::to_string(body.size()) + "\r\n";
  r += "Connection: close\r\n\r\n";
  if (post) r += body;
  return r;
}

bool parseHttpHead(const std::string& head, int* status, long* contentLength) {
  HttpResponse r;
  const size_t headEnd = head.find("\r\n\r\n");
  if (headEnd == std::string::npos) return false;
  // Reuse the whole-response parser on the head alone, with the length looked up separately.
  *contentLength = -1;
  size_t lineStart = head.find("\r\n") + 2;
  while (lineStart < headEnd + 2) {
    size_t lineEnd = head.find("\r\n", lineStart);
    std::string line = head.substr(lineStart, lineEnd - lineStart);
    size_t colon = line.find(':');
    if (colon != std::string::npos && lower(line.substr(0, colon)) == "content-length") {
      std::string value = line.substr(colon + 1);
      while (!value.empty() && value[0] == ' ') value.erase(0, 1);
      if (value.empty() || value.size() > 8) return false;
      long n = 0;
      for (char c : value) {
        if (c < '0' || c > '9') return false;
        n = n * 10 + (c - '0');
      }
      *contentLength = n;
    }
    lineStart = lineEnd + 2;
  }
  std::string statusLine = head.substr(0, head.find("\r\n")) + "\r\n\r\n";
  if (!parseHttpResponse(statusLine, &r)) return false;
  *status = r.status;
  return true;
}

bool parseHttpResponse(const std::string& raw, HttpResponse* out) {
  *out = HttpResponse();
  const size_t headEnd = raw.find("\r\n\r\n");
  if (headEnd == std::string::npos) return false;
  // "HTTP/1.x SSS ..."
  if (raw.compare(0, 7, "HTTP/1.") != 0 || raw.size() < 12 || raw[8] != ' ') return false;
  int status = 0;
  for (int i = 9; i < 12; ++i) {
    if (raw[i] < '0' || raw[i] > '9') return false;
    status = status * 10 + (raw[i] - '0');
  }
  if (status < 100 || status > 599 || (raw[12] != ' ' && raw[12] != '\r')) return false;
  long length = -1;
  size_t lineStart = raw.find("\r\n") + 2;
  while (lineStart < headEnd + 2) {
    size_t lineEnd = raw.find("\r\n", lineStart);
    std::string line = raw.substr(lineStart, lineEnd - lineStart);
    size_t colon = line.find(':');
    if (colon != std::string::npos) {
      std::string name = lower(line.substr(0, colon)), value = line.substr(colon + 1);
      while (!value.empty() && value[0] == ' ') value.erase(0, 1);
      if (name == "content-type") out->contentType = value;
      if (name == "content-length") {
        int n = 0;
        if (value.size() > 7 || value.empty()) return false;
        for (char c : value) {
          if (c < '0' || c > '9') return false;
          n = n * 10 + (c - '0');
        }
        length = n;
      }
    }
    lineStart = lineEnd + 2;
  }
  out->body = raw.substr(headEnd + 4);
  if (length >= 0) {
    if (out->body.size() < static_cast<size_t>(length)) return false;  // cut short
    out->body.resize(static_cast<size_t>(length));
  }
  out->status = status;
  return true;
}

BrainReply parseBrainReply(const HttpResponse& r) {
  BrainReply reply;
  if (r.status == 202) {
    reply.ok = reply.wait = true;
    return reply;
  }
  if (r.status == 204) {
    reply.ok = reply.end = true;
    return reply;
  }
  if (r.body.size() > 2048) {
    reply.error = "bad-reply";
    return reply;
  }
  auto f = fieldsOf(r.body);
  if (r.status != 200) {
    auto e = f.find("error");
    bool named = e != f.end() && !e->second.empty() && e->second.size() <= 20;
    if (named)
      for (char c : e->second) named = named && ((c >= 'a' && c <= 'z') || c == '-');
    reply.error = named ? e->second : "http-" + std::to_string(r.status);
    return reply;
  }
  auto has = [&](const char* k) { return f.find(k) != f.end(); };
  bool good = true;
  // A HAL with no voice (he chirps, or writes) is sent words and no audio; every other line
  // comes with its clip and how long it plays.
  const bool wordsOnly = has("who") && castFromId(f["who"]) >= 0 && !castVoiced(castFromId(f["who"]));
  if (has("text")) {
    reply.text = f["text"];
    good = good && printable(reply.text, 1, 150) && (wordsOnly ? !has("clip") : has("clip") && has("ms"));
  }
  if (has("clip")) {
    reply.clip = f["clip"];
    good = good && isHex(reply.clip, 32) && has("text");
  }
  if (has("ms")) good = good && smallNumber(f["ms"], 600000, &reply.ms);
  if (has("show")) {
    reply.show = f["show"];
    good = good && !reply.show.empty() && reply.show.size() <= 16;
    for (char c : reply.show) good = good && c >= 'a' && c <= 'z';
  }
  if (has("heard")) {  // what was said: kept to what HAL Zero can show and type back
    reply.hasHeard = true;
    for (unsigned char c : f["heard"])
      if (c >= 32 && c <= 126 && reply.heard.size() < 120) reply.heard.push_back(static_cast<char>(c));
  }
  if (has("who")) {
    reply.who = f["who"];
    good = good && castFromId(reply.who) >= 0 && has("text");
  }
  if (has("debate")) {
    reply.debate = f["debate"];
    good = good && isHex(reply.debate, 16);
  }
  if (has("last")) {
    good = good && (f["last"] == "0" || f["last"] == "1");
    reply.last = f["last"] == "1";
  }
  if (!has("text") && !has("heard") && !has("debate") && !has("ok")) good = false;  // it said nothing we know
  if (!good) {
    reply = BrainReply();
    reply.error = "bad-reply";
    return reply;
  }
  reply.ok = true;
  return reply;
}

std::string askBody(const std::string& facts, const std::string& question) {
  std::string body = facts;
  while (!body.empty() && body.back() == '\n') body.pop_back();
  return body + (body.empty() ? "\n" : "\n\n") + question;
}

}  // namespace hal

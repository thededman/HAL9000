#include "hal_facts.h"

#include <cctype>
#include <cstdio>

namespace hal {
namespace {

// Names that say more than the screens' short ones.
const char* nameFor(const Reading& r) {
  static const char* const names[][2] = {
      {"CPU", "processor load"}, {"MEM", "free memory"}, {"TMP", "chip temperature"}, {"NET", "wifi signal"},
      {"LNK", "link to the other HAL"}, {"CAM", "camera"}, {"TLT", "tilt"}, {"AUD", "lines spoken"},
      {"CLK", "time"}, {"UPT", "uptime"}, {"DSK", "the desk HAL"}, {"PWR", "battery"}, {"LUX", "room light"},
      {"PRX", "proximity"}, {"MOT", "motion"}};
  for (const auto& n : names)
    if (r.code == n[0]) return n[1];
  return nullptr;
}

void add(std::string* out, const std::string& name, const std::string& value) {
  if (!out->empty()) out->push_back('\n');
  *out += name + ": " + value;
}

std::string number(const char* format, int v) {
  char b[32];
  std::snprintf(b, sizeof b, format, v);
  return b;
}

}  // namespace

std::string factsFromReadings(const std::vector<Reading>& readings) {
  std::string out;
  for (const Reading& r : readings) {
    if (!r.valid || r.value.empty()) continue;
    std::string name;
    if (const char* better = nameFor(r)) {
      name = better;
    } else {
      for (char c : r.name) name.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    if (!name.empty()) add(&out, name, r.value);
  }
  return out;
}

std::string deskFacts(const PeerStatus* status, const PeerSensors* sensors) {
  std::string out;
  if (status) {
    char up[32];
    std::snprintf(up, sizeof up, "%d h %d min", status->upS / 3600, status->upS / 60 % 60);
    add(&out, "uptime", up);
    add(&out, "free memory", number("%d KB", status->heap / 1024));
    add(&out, "lines spoken", number("%d", status->lines));
    if (!status->mode.empty()) add(&out, "mode", status->mode);
    add(&out, "wifi signal", status->rssi == 0 ? std::string("off") : number("%d dBm", status->rssi));
    if (!status->fw.empty()) add(&out, "firmware", status->fw);
    add(&out, "camera motion", number("%d %%", status->cam));
  }
  if (sensors) {
    if (sensors->light >= 0) add(&out, "room light", number("%d %%", sensors->light));
    if (sensors->bat >= 0) add(&out, "battery", number("%d %%", sensors->bat) + (sensors->chg ? " and charging" : ""));
    add(&out, "tilt", number("%d degrees", sensors->tilt));
    add(&out, "chip temperature", number("%d C", sensors->temp));
    add(&out, "proximity", number("%d", sensors->prox));
  }
  return out;
}

std::string debateFacts(const std::vector<Reading>& zero, const PeerStatus* status, const PeerSensors* sensors) {
  std::string out;
  std::vector<Reading> mine;
  for (const Reading& r : zero) {
    if (r.code == "CLK") {
      if (r.valid) add(&out, "time", r.value);
    } else if (r.code != "DSK" && r.code != "LNK" && r.code != "CAM" && r.code != "TLT") {  // not worth arguing over
      mine.push_back(r);
    }
  }
  const auto addOwned = [&out](const char* owner, const std::string& facts) {
    size_t start = 0;
    while (start < facts.size()) {
      size_t end = facts.find('\n', start);
      if (end == std::string::npos) end = facts.size();
      const std::string line = facts.substr(start, end - start);
      if (line.compare(0, 13, "camera motion") != 0 && line.compare(0, 9, "proximity") != 0) {
        if (!out.empty()) out.push_back('\n');
        out += std::string(owner) + " " + line;
      }
      start = end + 1;
    }
  };
  addOwned("zero", factsFromReadings(mine));
  addOwned("desk", deskFacts(status, sensors));
  return out;
}

}  // namespace hal

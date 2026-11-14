#pragma once
// "Things you know right now": what HAL Zero tells the AI brain with each question, one
// "name: value" per line, so answers can use real readings.
#include <string>
#include <vector>

#include "hal_cast.h"
#include "hal_link.h"
#include "hal_readings.h"

namespace hal {

// HAL Zero's own readings (the ones the film-style screens show). Readings that
// aren't valid are left out.
std::string factsFromReadings(const std::vector<Reading>& readings);

// What is known about the desk HAL: his status (STATE) and sensors (SENSORS). Either may be null.
std::string deskFacts(const PeerStatus* status, const PeerSensors* sensors);

// For an argument: both HALs' readings, each line saying whose it is ("zero ...", "desk ..."),
// so neither quotes the other's numbers as his own. The time, first, belongs to both. Kept
// well under the brain's 24 lines.
std::string debateFacts(const std::vector<Reading>& zero, const PeerStatus* status, const PeerSensors* sensors);

// One of the other HALs in a conversation, and what he has reported (either may be null).
struct MemberFacts {
  int who = kDesk;  // a cast number (hal_cast.h)
  const PeerStatus* status = nullptr;
  const PeerSensors* sensors = nullptr;
};

// For a talk between several HALs: the time, then each one's most telling readings, each line
// saying whose it is ("nano battery: 81 %"). The brain takes `maxLines` at most, and they are
// shared out evenly, so no HAL is the one cut off the end. A HAL only says what his device can
// really tell: no room light from one with no light sensor.
std::string castFacts(const std::vector<Reading>& zero, const std::vector<MemberFacts>& others, int maxLines = 24);

}  // namespace hal

#pragma once
// "Things you know right now": what HAL Zero tells the AI brain with each question, one
// "name: value" per line, so answers can use real readings.
#include <string>
#include <vector>

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

}  // namespace hal

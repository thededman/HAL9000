#pragma once
// Loading HAL's settings and voice folders from whichever Storage the device has.
#include <string>
#include <vector>

#include "hal_config.h"
#include "hal_storage.h"
#include "hal_voice_index.h"

namespace hal {

// /hal/config.txt, or defaults (with a warning) if it's missing.
Config loadConfig(const Storage& storage, std::vector<std::string>* warnings);

// Every /hal/voice folder into the index; /hal/voice/mine joins the Tap pool.
// False if /hal/voice doesn't exist.
bool loadVoiceIndex(const Storage& storage, VoiceIndex& index);

}  // namespace hal

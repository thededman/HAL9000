#pragma once
// Reads HAL's settings and voice folders from the SD card.
#include "hal_config.h"
#include "hal_voice_index.h"

// Lists every /hal/voice folder into the index (mine/ is merged into Tap).
// Returns false if /hal/voice doesn't exist.
bool loadVoiceIndex(hal::VoiceIndex& index);

// /hal/config.txt, or defaults if it's missing. Warnings go to Serial.
hal::Config loadConfig();

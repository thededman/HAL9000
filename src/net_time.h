#pragma once
// HAL's clock: synced over Wi-Fi (NTP), kept in the CoreS3's RTC in UTC.
#include <functional>

#include "hal_config.h"

// Sets the system clock from the RTC and applies the config's time zone.
void restoreTimeFromRtc(const hal::Config& cfg);

// Connects to Wi-Fi, gets the time, writes it to the RTC, then turns Wi-Fi off
// (unless link=on: then it stays connected for HAL Zero).
// `whileWaiting` is called every ~30 ms so the eye keeps moving.
bool syncTime(const hal::Config& cfg, uint32_t timeoutMs, const std::function<void()>& whileWaiting);

// True once the clock holds a real date (2025 or later).
bool clockIsSet();

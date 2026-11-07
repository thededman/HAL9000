#include "net_time.h"

#include <M5Unified.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include <sys/time.h>
#include <time.h>

namespace {

void applyTz(const hal::Config& cfg) {
  setenv("TZ", cfg.timezone.c_str(), 1);
  tzset();
}

void waitMs(uint32_t ms, const std::function<void()>& whileWaiting) {
  for (uint32_t t0 = millis(); millis() - t0 < ms;) {
    if (whileWaiting) whileWaiting();
    delay(30);
  }
}

}  // namespace

bool clockIsSet() {
  time_t now = time(nullptr);
  struct tm t;
  gmtime_r(&now, &t);
  return t.tm_year + 1900 >= 2025;
}

void restoreTimeFromRtc(const hal::Config& cfg) {
  m5::rtc_datetime_t dt;
  if (M5.Rtc.isEnabled() && M5.Rtc.getDateTime(&dt) && dt.date.year >= 2025) {
    struct tm t = {};
    t.tm_year = dt.date.year - 1900;
    t.tm_mon = dt.date.month - 1;
    t.tm_mday = dt.date.date;
    t.tm_hour = dt.time.hours;
    t.tm_min = dt.time.minutes;
    t.tm_sec = dt.time.seconds;
    setenv("TZ", "UTC0", 1);  // the RTC holds UTC
    tzset();
    struct timeval tv = {mktime(&t), 0};
    settimeofday(&tv, nullptr);
    Serial.printf("  clock from RTC: %04d-%02d-%02d %02d:%02d UTC\n", dt.date.year, dt.date.month, dt.date.date,
                  dt.time.hours, dt.time.minutes);
  } else {
    Serial.println("  RTC has no valid time yet");
  }
  applyTz(cfg);
}

bool syncTime(const hal::Config& cfg, uint32_t timeoutMs, const std::function<void()>& whileWaiting) {
  if (cfg.wifiSsid.empty()) {
    Serial.println("  no wifi_ssid in config.txt, skipping time sync");
    return false;
  }
  uint32_t t0 = millis();
  WiFi.mode(WIFI_STA);
  WiFi.begin(cfg.wifiSsid.c_str(), cfg.wifiPassword.c_str());
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) waitMs(100, whileWaiting);
  bool ok = false;
  if (WiFi.status() == WL_CONNECTED) {
    configTzTime(cfg.timezone.c_str(), "pool.ntp.org", "time.nist.gov");
    time_t before = time(nullptr);
    // Wait for SNTP to move the clock (it may already look "set" from the RTC).
    while (millis() - t0 < timeoutMs && sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED) waitMs(100, whileWaiting);
    ok = sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED || (time(nullptr) != before && clockIsSet());
    if (ok) {
      time_t now = time(nullptr);
      struct tm utc;
      gmtime_r(&now, &utc);
      M5.Rtc.setDateTime(&utc);
      Serial.printf("  time synced: %s", asctime(&utc));
    } else {
      Serial.println("  wifi up, but no answer from the time servers");
    }
  } else {
    Serial.printf("  wifi '%s' didn't connect (status %d)\n", cfg.wifiSsid.c_str(), int(WiFi.status()));
  }
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  applyTz(cfg);
  return ok;
}

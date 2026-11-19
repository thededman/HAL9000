#pragma once
// How patient a HAL is when he fetches a clip from the AI brain (the desk HAL and HAL Card).
//
// On a home Wi-Fi a connection usually comes up in a hundredth of a second, but not always:
// with the computer on battery its radio dozes, single round trips reach half a second, and
// now and then the first packet is lost and resent a second or more later. A HAL who gave up
// after half a second (as both did until v0.7) lost lines that were there to be had. So the
// connection is given several seconds, and it is made without ever holding the HAL's loop up:
// his eye keeps moving and the link to HAL Zero stays answered while he waits.
#include <cstdint>

namespace hal {

constexpr uint32_t kFetchConnectMs = 4000;  // the connection may take this long (a first packet resent twice)
constexpr uint32_t kFetchStallMs = 3000;    // connected, and then nothing for this long
constexpr uint32_t kFetchTotalMs = 7000;    // the whole fetch: HAL Zero stops waiting for READY after 8 s

enum class FetchVerdict {
  Wait,      // carry on
  NoAnswer,  // the brain never answered the connection
  Stalled,   // it went quiet part-way
  TooLong,   // it is all taking too long
};

// `connected`: the connection is up. `startedMs`: when the fetch began. `progressMs`: when the
// connection came up or anything last arrived. Times are millis(): they may wrap.
inline FetchVerdict fetchVerdict(bool connected, uint32_t startedMs, uint32_t progressMs, uint32_t nowMs) {
  if (nowMs - startedMs > kFetchTotalMs) return FetchVerdict::TooLong;
  if (!connected) return nowMs - startedMs > kFetchConnectMs ? FetchVerdict::NoAnswer : FetchVerdict::Wait;
  return nowMs - progressMs > kFetchStallMs ? FetchVerdict::Stalled : FetchVerdict::Wait;
}

}  // namespace hal

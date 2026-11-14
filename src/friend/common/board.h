#pragma once
// What a friend's device does for him. Each board has its own folder with one implementation
// of these (src/friend/nano, card, paper), and a build takes just one folder: no #ifdefs.
#include <cstdint>
#include <string>

#include "hal_link.h"

namespace board {

extern const int kWho;           // which HAL this is (a cast number, hal_cast.h)
extern const char* const kHost;  // his name on the network: "hal-nano" (hal-nano.local)
extern const bool kWifiSleep;    // let the radio doze between packets (he runs on a small battery)

void begin();                                             // after M5.begin(): his screen and sound
void update(uint32_t nowMs);                              // every pass: sound, animation, the screen
bool tapped();                                            // his button went down since the last call
void idle(const std::string& status);                     // nobody is speaking: his eye, and this line under his name
void showLine(int who, int n, const std::string& text);   // someone is speaking: who, and the words
void say(const std::string& text, uint32_t ms);           // his own line: chirp it (or write it) over `ms`
void stop();                                              // stop saying it
void idleTap();                                           // a tap with nobody talking
// What his own sensors can add to the numbers he reports (either may be null).
void readings(hal::PeerStatus* status, hal::PeerSensors* sensors);

}  // namespace board

#pragma once
// Scripted conversations between the desk HAL (OLD) and HAL Zero (NEW).
//
//   TITLE: Who's the better model
//   OLD: Good evening. I don't believe we've met.
//   PAUSE: 1200          <- extra silence before the next line, in ms (0..10000)
//   NEW: We haven't. I'm the upgrade.
//   # comments and blank lines are ignored
#include <string>
#include <vector>

#include "hal_cast.h"

namespace hal {

enum class Speaker { Old, New };  // in a script: the desk HAL's line, or HAL Zero's

struct DialogueLine {
  Speaker who;
  std::string text;
  int pauseMs = 0;   // silence before this line; 0 = the conductor's normal gap
  int peer = kDesk;  // which HAL says a line that isn't HAL Zero's (a script's are all the desk HAL's)
  int speaker() const { return who == Speaker::New ? kZero : peer; }  // as a cast number (hal_cast.h)
};

struct Dialogue {
  std::string id;
  std::string title;
  std::vector<DialogueLine> lines;  // line n (1-based) is lines[n - 1]
};

constexpr int kMaxDialogueLines = 99;

// [a-z0-9-]{1,32}: safe to use in file paths and over the network.
bool isValidDialogueId(const std::string& id);

// False (with "line N: ..." messages) if the script has problems.
bool parseDialogue(const std::string& id, const std::string& text, Dialogue* out, std::vector<std::string>* errors);

// "/hal/voice/dialogue/<id>/007-old.wav"
std::string dialogueVoicePath(const std::string& id, int n, Speaker who);

}  // namespace hal

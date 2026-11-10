#pragma once
// HAL's front camera (GC0308). Frames stay in RAM: nothing is saved or sent.
#include <M5Unified.h>

// Starts the camera. False (and logged) if it doesn't answer.
bool cameraBegin();

// At most every 66 ms: grabs a frame and refreshes the motion score, the
// moving-area box and the full frame (for the Visual Analysis screen). True if a new frame arrived.
// Only call after cameraBegin() returned true.
bool cameraPoll(uint32_t nowMs);

int cameraScore();          // 0..255: share of the picture that changed (1% = 5 points)
// Moving area in 160x120 frame coordinates; false when too little changed.
bool cameraBox(int* x, int* y, int* w, int* h);
const uint16_t* cameraFrame();  // latest 160x120 frame, byte-swapped RGB565, or nullptr

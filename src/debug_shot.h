#pragma once
// Debug helper: send 'S' over USB serial and the CoreS3 replies with a raw
// screenshot ("SHOT 320 240\n" + 320*240 RGB565 pixels, big-endian), so the
// screen can be checked from the Mac.
// Returns any other command character received (or 0) for the caller to handle.
int serviceDebugCommands();

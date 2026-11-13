#pragma once
// Window and keyboard through SDL2: a 320x170 canvas shown scaled in a window on
// the Mac, or full-screen on the Cardputer Zero. Keys arrive as down/up events,
// which HAL's brain needs (a tap is a short press, a hold starts the plea).
#include <M5GFX.h>
#include <SDL.h>

#include <string>
#include <utility>

#include <vector>

struct KeyInput {
  bool quit = false;                // window closed
  bool spaceDown = false;           // Space is held right now (HAL Zero's "finger")
  std::vector<SDL_Keycode> pressed;  // keys pressed since the last poll (no auto-repeat)
  std::string typed;                // characters typed since the last poll (for the ask box)
  bool talkDown = false;            // the talk key is held: Fn by itself on the Cardputer, right Alt in a window
  bool talkDropped = false;         // ... it turned out to be Fn plus another key: not talking after all
  // Every key event since the last poll, as the keyboard sent it: its code (a Linux input code on
  // the Cardputer, an SDL scancode in a window) and whether it went down. For --keys.
  std::vector<std::pair<int, bool>> raw;
};

class SdlIo {
 public:
  bool open(int w, int h, bool fullscreen, int scale);
  void present(M5Canvas& frame);  // the canvas holds byte-swapped RGB565
  void poll(KeyInput* keys);
  void close();

 private:
  SDL_Window* window_ = nullptr;
  SDL_Renderer* renderer_ = nullptr;
  SDL_Texture* texture_ = nullptr;
  std::vector<uint16_t> pixels_;
  bool space_ = false, talk_ = false;
  int w_ = 0, h_ = 0;
};

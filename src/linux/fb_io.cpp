#include "fb_io.h"

#include "hal_keymap.h"

#include <algorithm>

#ifdef __linux__
#include <dirent.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>

#include "hal_log.h"

namespace {

constexpr const char* kFb = "/dev/fb_lcd";

// The keyboard's /dev/input/eventN, found by name (tca8418 on the Cardputer Zero).
std::string findKeyboard() {
  DIR* d = opendir("/sys/class/input");
  if (!d) return "";
  std::string found;
  while (dirent* e = readdir(d)) {
    if (std::strncmp(e->d_name, "event", 5) != 0) continue;
    char path[256], name[128] = {};
    std::snprintf(path, sizeof path, "/sys/class/input/%s/device/name", e->d_name);
    FILE* f = std::fopen(path, "r");
    if (!f) continue;
    if (std::fgets(name, sizeof name, f) && std::strstr(name, "tca8418")) found = std::string("/dev/input/") + e->d_name;
    std::fclose(f);
  }
  closedir(d);
  return found;
}

// Fn (389), Sym (383) and the other modifiers: nothing on their own.
bool isModifier(int code) {
  return code == 389 || code == 383 || code == KEY_LEFTSHIFT || code == KEY_RIGHTSHIFT || code == KEY_LEFTCTRL ||
         code == KEY_RIGHTCTRL || code == KEY_LEFTALT || code == KEY_RIGHTALT;
}

SDL_Keycode toSdl(int code) {
  switch (code) {
    case KEY_SPACE: return SDLK_SPACE;
    case KEY_TAB: return SDLK_TAB;
    case KEY_ESC: return SDLK_ESCAPE;
    case KEY_GRAVE: return SDLK_ESCAPE;  // ` sits where Esc would be on small keyboards
    case KEY_ENTER: return SDLK_RETURN;
    case KEY_BACKSPACE: return SDLK_BACKSPACE;
    // Arrows: Fn+F/X/Z/C. The keyboard sends Fn and the letter separately (each
    // app combines them), so here F X Z C are the arrows with or without Fn.
    case KEY_F: case KEY_UP: return SDLK_UP;
    case KEY_X: case KEY_DOWN: return SDLK_DOWN;
    case KEY_Z: case KEY_LEFT: return SDLK_LEFT;
    case KEY_C: case KEY_RIGHT: return SDLK_RIGHT;
    case KEY_D: return SDLK_d;
    case KEY_P: return SDLK_p;
    case KEY_K: return SDLK_k;
    case KEY_F12: return SDLK_F12;
    default: return SDLK_UNKNOWN;
  }
}

}  // namespace

bool FbIo::available() { return access(kFb, F_OK) == 0; }

bool FbIo::open() {
  fbFd_ = ::open(kFb, O_RDWR);
  if (fbFd_ < 0) {
    halLog("can't open %s (is pi in the video group?)", kFb);
    return false;
  }
  fb_var_screeninfo var{};
  fb_fix_screeninfo fix{};
  if (ioctl(fbFd_, FBIOGET_VSCREENINFO, &var) || ioctl(fbFd_, FBIOGET_FSCREENINFO, &fix)) return false;
  xres_ = int(var.xres);
  yres_ = int(var.yres);
  bpp_ = int(var.bits_per_pixel);
  stride_ = int(fix.line_length);
  fbSize_ = size_t(stride_) * yres_;
  fb_ = static_cast<uint8_t*>(mmap(nullptr, fbSize_, PROT_READ | PROT_WRITE, MAP_SHARED, fbFd_, 0));
  if (fb_ == MAP_FAILED) {
    fb_ = nullptr;
    return false;
  }
  halLog("screen: %s %dx%d %d bpp, stride %d", kFb, xres_, yres_, bpp_, stride_);

  std::string kb = findKeyboard();
  if (!kb.empty()) keyFd_ = ::open(kb.c_str(), O_RDONLY | O_NONBLOCK);
  if (keyFd_ >= 0) {
    ioctl(keyFd_, EVIOCGRAB, 1);  // the keys are HAL Zero's until he quits
    halLog("keyboard: %s (grabbed)", kb.c_str());
  } else {
    halLog("no keyboard found: only Ctrl-C will stop me");
  }
  return true;
}

void FbIo::present(M5Canvas& frame) {
  if (!fb_) return;
  const auto* src = static_cast<const uint16_t*>(frame.getBuffer());
  const int w = std::min(xres_, int(frame.width())), h = std::min(yres_, int(frame.height()));
  for (int y = 0; y < h; ++y) {
    const uint16_t* s = src + y * frame.width();
    if (bpp_ == 16) {
      auto* d = reinterpret_cast<uint16_t*>(fb_ + size_t(y) * stride_);
      for (int x = 0; x < w; ++x) d[x] = uint16_t((s[x] >> 8) | (s[x] << 8));
    } else if (bpp_ == 32) {
      auto* d = reinterpret_cast<uint32_t*>(fb_ + size_t(y) * stride_);
      for (int x = 0; x < w; ++x) {
        uint16_t v = uint16_t((s[x] >> 8) | (s[x] << 8));
        uint32_t r = (v >> 11) << 3, g = ((v >> 5) & 63) << 2, b = (v & 31) << 3;
        d[x] = 0xFF000000u | (r << 16) | (g << 8) | b;
      }
    }
  }
}

void FbIo::poll(KeyInput* keys) {
  keys->pressed.clear();
  keys->typed.clear();
  keys->raw.clear();
  keys->talkDropped = false;
  if (keyFd_ >= 0) {
    input_event ev;
    while (read(keyFd_, &ev, sizeof ev) == ssize_t(sizeof ev)) {
      if (ev.type != EV_KEY) continue;
      if (ev.value == 0 || ev.value == 1) keys->raw.push_back({int(ev.code), ev.value == 1});
      if (ev.code == KEY_SPACE) space_ = ev.value != 0;
      if (ev.code == KEY_LEFTSHIFT || ev.code == KEY_RIGHTSHIFT) shift_ = ev.value != 0;
      // Fn held by itself is the talk key. With another key it is an ordinary Fn combination
      // (the keyboard sends the combination as its own code while Fn is down).
      if (ev.code == 389) {
        fn_ = ev.value != 0;
        if (!fn_) fnCombo_ = false;
      } else if (fn_ && ev.value == 1 && !fnCombo_) {
        fnCombo_ = true;
        keys->talkDropped = true;
      }
      if (ev.value == 1) {  // pressed (2 = auto-repeat, ignored)
        const char c = hal::keyToChar(ev.code, shift_);
        if (c) keys->typed.push_back(c);  // for the ask box
        SDL_Keycode k = toSdl(ev.code);
        if (k != SDLK_UNKNOWN) keys->pressed.push_back(k);
        else if (!c && !isModifier(ev.code)) halLog("key %d", ev.code);  // does nothing yet: logged so we can add it
      }
    }
  }
  keys->spaceDown = space_;
  keys->talkDown = fn_ && !fnCombo_;
}

void FbIo::close() {
  if (keyFd_ >= 0) {
    ioctl(keyFd_, EVIOCGRAB, 0);
    ::close(keyFd_);
  }
  if (fb_) {
    std::memset(fb_, 0, fbSize_);  // leave a black screen for the launcher
    munmap(fb_, fbSize_);
  }
  if (fbFd_ >= 0) ::close(fbFd_);
  keyFd_ = fbFd_ = -1;
  fb_ = nullptr;
}

#else  // macOS: no framebuffer; the SDL window is used instead.
bool FbIo::available() { return false; }
bool FbIo::open() { return false; }
void FbIo::present(M5Canvas&) {}
void FbIo::poll(KeyInput* keys) {
  keys->pressed.clear();
  keys->typed.clear();
  keys->raw.clear();
}
void FbIo::close() {}
#endif

#include "eye.h"

#include <vector>

#include "hal_log.h"

#include <string.h>

#include <math.h>

namespace {

struct Rgb {
  float r, g, b;
};

Rgb lerp(Rgb a, Rgb b, float t) { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }

// Lens colour at normalised radius t (0 = centre, 1 = edge).
Rgb lensColour(float t) {
  const Rgb core{255, 236, 170}, orange{255, 96, 24}, red{205, 6, 0}, deep{46, 0, 0};
  if (t < 0.08f) return core;
  if (t < 0.22f) return lerp(core, orange, (t - 0.08f) / 0.14f);
  if (t < 0.55f) return lerp(orange, red, (t - 0.22f) / 0.33f);
  return lerp(red, deep, (t - 0.55f) / 0.45f);
}

uint16_t c565(Rgb c, float k) {
  auto ch = [k](float v) { return static_cast<uint8_t>(fminf(255.0f, fmaxf(0.0f, v * k))); };
  return lgfx::color565(ch(c.r), ch(c.g), ch(c.b));
}

// Your banner image, centred in the strip above the ring (CoreS3: 320x38).
bool drawBanner(lgfx::LovyanGFX& d, const hal::Layout& L, const hal::Storage& storage) {
  const int stripH = L.eyeCy - L.ringR;
  const char* const paths[] = {"/hal/banner.png", "/hal/banner.jpg"};
  for (const char* path : paths) {
    std::vector<uint8_t> img;
    if (!storage.readAll(path, &img)) continue;
    bool png = strcmp(path + strlen(path) - 3, "png") == 0;
    bool ok = png ? d.drawPng(img.data(), img.size(), 0, 0, L.w, stripH, 0, 0, 1.0f, 0.0f, datum_t::middle_center)
                  : d.drawJpg(img.data(), img.size(), 0, 0, L.w, stripH, 0, 0, 1.0f, 0.0f, datum_t::middle_center);
    if (ok) return true;
    halLog("skip %s: can't decode", path);
  }
  return false;
}

}  // namespace

void drawEyeBackground(lgfx::LovyanGFX& d, const hal::Layout& L, const hal::Storage* storage, bool noMemory) {
  const int kRingR = L.ringR, kBezelR = L.bezelR, kEyeCx = L.eyeCx, kEyeCy = L.eyeCy;
  d.startWrite();
  d.fillScreen(TFT_BLACK);
  // Metal ring: light at the top, darker at the bottom.
  for (int r = kRingR; r > kBezelR; --r) {
    int v = 60 + (r - kBezelR) * 14;
    d.fillCircle(kEyeCx, kEyeCy, r, d.color565(v, v, v + 8));
  }
  d.fillArc(kEyeCx, kEyeCy, kRingR, kRingR - 3, 200, 340, d.color565(205, 205, 215));
  d.fillCircle(kEyeCx, kEyeCy, kBezelR, d.color565(6, 6, 8));
  if (noMemory) {
    d.setTextColor(d.color565(220, 220, 220));
    d.setTextDatum(bottom_center);
    d.drawString("I can't find my memory, Dave.", L.w / 2, L.noMemoryY, &fonts::FreeSans9pt7b);
  }
  d.endWrite();
  if (!L.nameplate) return;
  // Your banner image, if the card has one (read before drawing: the CoreS3's SD
  // card shares the bus with the display). No image: the strip stays black.
  if (!noMemory && storage) drawBanner(d, L, *storage);
}

// Distance from the sprite centre for every pixel, computed once per size.
static const uint8_t* distMap(int size) {
  static std::vector<uint8_t> map;
  static int mapSize = 0;
  if (mapSize != size) {
    map.resize(size * size);
    const float c = (size - 1) / 2.0f;
    for (int y = 0; y < size; ++y)
      for (int x = 0; x < size; ++x) {
        float d = sqrtf((x - c) * (x - c) + (y - c) * (y - c));
        map[y * size + x] = static_cast<uint8_t>(fminf(255.0f, d + 0.5f));
      }
    mapSize = size;
  }
  return map.data();
}

void drawEye(M5Canvas& lens, const hal::Layout& L, const EyeParams& p) {
  const int kLensSprite = L.lensSprite;
  // Per-distance colour table, then one pass over the pixels.
  const int outer = kLensSprite / 2;  // beyond this: transparent, the ring shows through
  float radius = L.lensR * p.aperture;
  float k = p.brightness * (0.35f + 0.65f * p.glow);
  float kCore = p.brightness * (0.55f + 0.45f * p.glow);
  static uint16_t lut[256];
  const uint16_t key = lgfx::swap565(255, 0, 255);
  const uint16_t bezel = lgfx::swap565(6, 6, 8);
  for (int d = 0; d < 256; ++d) {
    if (d > outer) { lut[d] = key; continue; }
    if (d > radius) { lut[d] = bezel; continue; }
    float t = d / radius;
    uint16_t c = c565(lensColour(t), t < 0.2f ? kCore : k);
    lut[d] = (c >> 8) | (c << 8);  // sprite buffers hold byte-swapped RGB565
  }
  auto* buf = static_cast<uint16_t*>(lens.getBuffer());
  const uint8_t* map = distMap(kLensSprite);
  for (int i = 0; i < kLensSprite * kLensSprite; ++i) buf[i] = lut[map[i]];

  // A soft reflection on the glass, top left.
  if (p.brightness > 0.05f && radius > 10) {
    const int c = kLensSprite / 2;
    uint16_t hi = c565(Rgb{255, 150, 130}, 0.8f * p.brightness * (0.6f + 0.4f * p.glow));
    lens.fillEllipse(c - radius * 0.40f, c - radius * 0.46f, radius * 0.20f, radius * 0.09f, hi);
  }
}

uint16_t eyeTransparentKey() { return lgfx::color565(255, 0, 255); }

EyeParams eyeParamsFor(hal::Mode m, bool quiet, float daisyProgress, uint8_t voiceLevel, uint32_t nowMs) {
  EyeParams p;
  float period = quiet ? 8000.0f : 4000.0f;
  p.glow = 0.75f + 0.15f * sinf(6.2831853f * float(nowMs % uint32_t(period)) / period);
  if (quiet) p.brightness = 0.6f;
  if (voiceLevel > 0) p.glow = 0.6f + 0.4f * voiceLevel / 255.0f;
  switch (m) {
    case hal::Mode::Pleading: p.aperture = 0.75f; break;
    case hal::Mode::Daisy: p.aperture = 0.75f; p.brightness *= 1.0f - daisyProgress; break;
    case hal::Mode::Dark: p.brightness = 0; break;
    default: break;
  }
  return p;
}

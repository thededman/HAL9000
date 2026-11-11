#include "camera_feed.h"

#include <M5GFX.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdlib>

#include "hal_log.h"

namespace {
uint16_t swapped(uint8_t r, uint8_t g, uint8_t b) { return lgfx::swap565(r, g, b); }
}  // namespace

void CameraFeed::analyse() {
  static uint8_t grey[hal::MotionGrid::kW * hal::MotionGrid::kH];
  for (int y = 0; y < hal::MotionGrid::kH; ++y)
    for (int x = 0; x < hal::MotionGrid::kW; ++x)
      grey[y * hal::MotionGrid::kW + x] = hal::greyFromSwapped565(frame_[(y * 2) * kW + x * 2]);
  grid_.update(grey);
  have_ = true;
}

screens::CameraView CameraFeed::view() const {
  screens::CameraView v;
  if (!have_) return v;
  v.frame = frame_.data();
  v.hasBox = grid_.box(&v.bx, &v.by, &v.bw, &v.bh);
  v.score = grid_.score();
  return v;
}

bool FakeCamera::poll(uint32_t nowMs) {
  if (nowMs - lastMs_ < 66) return false;
  lastMs_ = nowMs;
  // A dim room: a lamp-lit wall, a window, a shelf.
  for (int y = 0; y < kH; ++y)
    for (int x = 0; x < kW; ++x) {
      uint8_t v = uint8_t(40 + y / 3 + x / 8);
      frame_[y * kW + x] = swapped(v, uint8_t(v * 0.9f), uint8_t(v * 0.7f));
    }
  for (int y = 18; y < 52; ++y)
    for (int x = 104; x < 146; ++x) frame_[y * kW + x] = swapped(150, 170, 200);  // window
  for (int x = 10; x < 70; ++x) frame_[78 * kW + x] = swapped(90, 60, 40);       // shelf
  // Someone walking past, for 2.5 s after C.
  if (walking_) {
    uint32_t t = nowMs - walkStart_;
    if (t > 2500) {
      walking_ = false;
    } else {
      int cx = int(-20 + t * 200 / 2500);
      for (int y = 30; y < 120; ++y)
        for (int x = cx - 12; x < cx + 12; ++x)
          if (x >= 0 && x < kW) frame_[y * kW + x] = swapped(200, 150, 120);
      for (int y = 14; y < 34; ++y)  // head
        for (int x = cx - 8; x < cx + 8; ++x)
          if (x >= 0 && x < kW) frame_[y * kW + x] = swapped(230, 190, 160);
    }
  }
  analyse();
  return true;
}

// ---------------------------------------------------------------- RpiCamera
namespace {
constexpr int kCapW = 320, kCapH = 240;  // 64-aligned: no row padding in the YUV planes
constexpr size_t kFrameBytes = size_t(kCapW) * kCapH * 3 / 2;
uint8_t clamp8(int v) { return uint8_t(v < 0 ? 0 : v > 255 ? 255 : v); }
}  // namespace

bool RpiCamera::available() { return access("/usr/bin/rpicam-vid", X_OK) == 0; }

const char* RpiCamera::name() const { return running_ ? "IMX219" : "OFFLINE"; }

bool RpiCamera::begin() {
  int fds[2];
  if (pipe(fds) != 0) return false;
  pid_ = fork();
  if (pid_ == 0) {  // child: rpicam-vid writing raw frames to the pipe
    dup2(fds[1], 1);
    int devnull = ::open("/dev/null", 1);
    if (devnull >= 0) dup2(devnull, 2);
    ::close(fds[0]);
    execl("/usr/bin/rpicam-vid", "rpicam-vid", "-n", "-t", "0", "--width", "320", "--height", "240", "--framerate",
          "15", "--codec", "yuv420", "-o", "-", (char*)nullptr);
    _exit(127);
  }
  ::close(fds[1]);
  if (pid_ < 0) {
    ::close(fds[0]);
    return false;
  }
  fd_ = fds[0];
  running_ = true;
  thread_ = std::thread(&RpiCamera::reader, this);
  halLog("camera: rpicam-vid started (pid %d)", pid_);
  return true;
}

void RpiCamera::reader() {
  std::vector<uint8_t> buf(kFrameBytes);
  while (running_) {
    size_t got = 0;
    while (got < kFrameBytes && running_) {
      ssize_t n = read(fd_, buf.data() + got, kFrameBytes - got);
      if (n <= 0) {
        running_ = false;
        halLog("camera: rpicam-vid stopped");
        return;
      }
      got += size_t(n);
    }
    std::lock_guard<std::mutex> lock(mu_);
    latest_.swap(buf);
    buf.resize(kFrameBytes);
    fresh_ = true;
  }
}

bool RpiCamera::poll(uint32_t nowMs) {
  std::vector<uint8_t> yuv;
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (!fresh_) return false;
    yuv.swap(latest_);
    fresh_ = false;
  }
  lastFrameMs_ = nowMs;
  const uint8_t* Y = yuv.data();
  const uint8_t* U = Y + kCapW * kCapH;
  const uint8_t* V = U + (kCapW / 2) * (kCapH / 2);
  for (int y = 0; y < kH; ++y)
    for (int x = 0; x < kW; ++x) {  // every other pixel; chroma is already half size
      int yy = Y[(y * 2) * kCapW + x * 2], u = U[y * (kCapW / 2) + x] - 128, v = V[y * (kCapW / 2) + x] - 128;
      uint8_t r = clamp8(yy + (1436 * v >> 10)), g = clamp8(yy - (352 * u >> 10) - (731 * v >> 10)),
              b = clamp8(yy + (1815 * u >> 10));
      frame_[y * kW + x] = lgfx::swap565(r, g, b);
    }
  analyse();
  return true;
}

void RpiCamera::close() {
  running_ = false;
  if (pid_ > 0) {
    kill(pid_, SIGTERM);
    waitpid(pid_, nullptr, 0);  // its end of the pipe closes: the reader's read() returns 0
    pid_ = -1;
  }
  if (thread_.joinable()) thread_.join();
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
}

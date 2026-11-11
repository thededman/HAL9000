# Cardputer Zero: device notes (Phase 0)

Checked read-only over SSH (`david@cardputerzero.local`) on 2026-11-11.

| Area | Found | Choice for HAL Zero |
|---|---|---|
| System | Debian 13 (trixie), kernel 6.18.39+rpt-rpi-v8, aarch64, 4 cores | — |
| Memory | 352 MB total, ~99 MB available; 351 MB swap | **Build in Docker on the Mac** (arm64 `debian:trixie`), copy the binary over; compiling M5GFX on the device would crawl |
| Screen | `/dev/fb0` = `panel-mipi-dbid` 320x170 16 bpp (DRM `card1-SPI-1`) | SDL2 window, full-screen |
| Desktop | `lightdm` → `labwc` (Wayland) as user **pi**, running M5Stack's `M5CardputerZero-APPLaunch` | HAL Zero runs as `pi` inside that session (`XDG_RUNTIME_DIR=/run/user/1000`), like the launcher's other apps |
| Launcher apps | `/usr/share/APPLaunch/applications/*.desktop` (`Exec=`, `Icon=share/images/x.png` relative to `/usr/share/APPLaunch`) | Install `hal-zero.desktop` + icon there (needs sudo) |
| Launcher process | systemd **user** service `APPLaunch.service` (pi); icons are 100×100 RGBA PNGs; pi's `sudo` asks for a password | `tools/cardputer.sh install` copies the entry with sudo (David types the password), then `systemctl --user restart APPLaunch`. The start script `~/hal-zero/hal-zero` pauses the launcher (SIGSTOP) while HAL Zero has the screen and resumes it when he quits |
| Camera at run time | `rpicam-vid` 320×240 YUV420 at 15 fps through a pipe: HAL Zero ~15% CPU, ~130 MB RAM still available | Live Visual Analysis confirmed on the LCD; quitting leaves no `rpicam-vid` behind |
| Motion sensor axes | Sitting on the desk, the BMI270 reads gravity on **x** (−1 g), not z | The HUD's TILT is measured from where HAL first settled (`MotionDetector::tiltFromHomeDeg`), so mounting doesn't matter |
| Audio | card 1 `ES8389` (built-in), card 0 HDMI | SDL audio through the session |
| Camera | `imx219` via libcamera; `rpicam-vid` 1.13 installed | `rpicam-vid` child process, YUV420 |
| IMU | IIO `iio:device1` = `bmi270` (`in_accel_*_raw`, `in_anglvel_*_raw`); `iio:device2` = `bmm150` | IIO sysfs reader |
| Keyboard | `tca8418c` (event3) | SDL key events via Wayland |
| SDL2 | runtime `libSDL2-2.0.so.0.3200.4` installed; `libsdl2-dev` not | Only the runtime is needed when building in Docker |
| Toolchain | gcc/g++ 14.2, make, pkg-config, Python 3.13, git, rsync; no cmake | — |
| Clock | RTC present, NTP synced; **time zone `Etc/GMT-4` (UTC+4)** | HAL Zero applies `timezone` from its config.txt (default New York) |

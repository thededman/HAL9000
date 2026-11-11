# Time log

One line per working day. Hours include time outside Claude sessions (testing,
flashing, fiddling with the device). At each release, cross-check against
`python3 ~/.claude/scripts/claude-hours.py HAL9000`.

| Date | Hours | What was done |
|---|---|---|
| 2026-11-06 | 2.2 | v0.1.0 (20:36–21:55): design, voice samples (Piper "Alan", dry close-mic), spec, plan, logic and tests, 85 lines, firmware (eye, consoles, sensors, Wi-Fi clock), on-device checks, release. Camera (21:55–22:50): spike on the device, design, plan, settings, arrival detection, 20 camera lines, preview, Visual Analysis, card update. Claude sessions measured 2.2 h; card swaps and hands-on testing were inside those sessions |
| 2026-11-08 | 0.4 | Camera: walk-in fix (moving HAL no longer counts), README, final review and fixes, v0.2.0. HAL voice (18:25–18:32): HAL-9000 Piper model replaces Alan, voice samples compared, 105 clips re-rendered |
| 2026-11-09 | 0.4 | Audio cut-off investigation (not reproduced), camera preview off the main screen, optional banner image, Cardputer Zero planning |
| 2026-11-10 | 4.5 | v0.3.0: HAL Zero on the Cardputer Zero (shared UI library, Mac simulator, Docker cross-build, framebuffer and keyboard, SSH set-up with David, launcher entry, real camera and motion sensor, arrow keys), the link and conversation conductor, HAL Zero's voice and three dialogues (0.7 h in a parallel session), first live two-HAL conversation, export, grey plate removed; v0.4.0: screens design, spec and plan, link v2 (status, liveness), screen catalogue, nine new screens, 45 new lines, light sensor fix, final review and fixes, checks on both devices with David (card swaps, flashing, unplug test). Claude sessions measured 4.5 h; device testing with David was inside those sessions |

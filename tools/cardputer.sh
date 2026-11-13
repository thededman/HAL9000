#!/usr/bin/env bash
# HAL Zero on the Cardputer Zero.
#   tools/cardputer.sh build     build in Docker (arm64 Debian 13, like the device)
#   tools/cardputer.sh deploy    copy the program, start script and launcher entry to the device (as pi)
#   tools/cardputer.sh install   add HAL Zero to M5Stack's launcher (once; asks for pi's sudo password)
#   tools/cardputer.sh run [ARGS] start it on the device's screen (Esc quits; the launcher comes back);
#                                e.g. run --show wall, run --ask "A question for the AI brain", run --keys
#   tools/cardputer.sh stop      quit it
#   tools/cardputer.sh log       the last lines of its log
#   tools/cardputer.sh look      make it look around (Visual Analysis, the live camera)
#   tools/cardputer.sh shot      screenshot what it's showing, copied to export/cardputer/
# HOST defaults to pi@cardputerzero.local.
set -euo pipefail
cd "$(dirname "$0")/.."
HOST="${HOST:-pi@cardputerzero.local}"
case "${1:-}" in
  build)
    # Always arm64, like the Cardputer: native on Apple silicon, emulated on Intel/AMD (see docs/SETUP.md).
    docker image inspect hal-zero-build >/dev/null 2>&1 || docker build -q --platform linux/arm64 -t hal-zero-build tools/cardputer >/dev/null
    docker run --rm --platform linux/arm64 -v "$PWD":/src -v hal-zero-pio:/opt/pio-core hal-zero-build pio run -e cardputer
    file .pio/build/cardputer/program
    ;;
  deploy)
    ssh "$HOST" 'mkdir -p ~/hal-zero ~/hal/shots'
    # Copy under a temporary name, then swap it in: a dropped connection mid-copy never
    # leaves a half-written program behind.
    scp -q .pio/build/cardputer/program "$HOST":hal-zero/hal9000.new
    ssh "$HOST" 'chmod +x ~/hal-zero/hal9000.new && mv -f ~/hal-zero/hal9000.new ~/hal-zero/hal9000'
    scp -q tools/cardputer/hal-zero tools/cardputer/hal-zero.desktop tools/cardputer/hal-zero.png "$HOST":hal-zero/
    # HAL Zero's settings: only if there are none yet (never overwrites yours). The Cardputer
    # decides, in one step: a connection that fails while asking must not look like "none yet".
    ssh "$HOST" '[ -e ~/hal/config.txt ] || { cat > ~/hal/config.txt.new && mv ~/hal/config.txt.new ~/hal/config.txt && echo "settings: ~/hal/config.txt"; }' < tools/cardputer/config.txt
    echo "deployed to $HOST:~/hal-zero/"
    ;;
  run)
    shift
    if ssh "$HOST" 'pgrep -x hal9000 >/dev/null'; then
      echo "HAL Zero is already running on the device (tools/cardputer.sh stop quits him)"; exit 1
    fi
    args=""
    for a in "$@"; do args="$args $(printf '%q' "$a")"; done  # keep "a question with spaces" as one argument
    ssh "$HOST" "setsid -f ~/hal-zero/hal-zero$args > /dev/null 2>&1 < /dev/null"
    echo "started; log: tools/cardputer.sh log"
    ;;
  install)
    ssh -t "$HOST" 'sudo install -m 644 ~/hal-zero/hal-zero.desktop /usr/share/APPLaunch/applications/ &&
      sudo install -m 644 ~/hal-zero/hal-zero.png /usr/share/APPLaunch/share/images/ &&
      XDG_RUNTIME_DIR=/run/user/$(id -u) systemctl --user restart APPLaunch &&
      echo "HAL Zero is in the launcher"'
    ;;
  stop)  # waits (up to 10 s) until he has gone, camera and all
    ssh "$HOST" 'pkill -TERM -x hal9000 || { echo "not running"; exit 0; }
      for i in $(seq 50); do pgrep -x hal9000 >/dev/null || exit 0; sleep 0.2; done; echo "still running"'
    ;;
  log) ssh "$HOST" 'tail -n 40 /tmp/hal-zero.log' ;;
  look) ssh "$HOST" 'pkill -USR2 -x hal9000 || echo "not running"' ;;
  shot)
    name=$(ssh "$HOST" 'pkill -USR1 -x hal9000 && sleep 1 && ls -t ~/hal/shots | head -1')
    mkdir -p export/cardputer
    scp -q "$HOST:hal/shots/$name" export/cardputer/ && echo "export/cardputer/$name"
    ;;
  *) sed -n 2,12p "$0"; exit 1 ;;
esac

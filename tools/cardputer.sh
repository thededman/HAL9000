#!/usr/bin/env bash
# HAL Zero on the Cardputer Zero.
#   tools/cardputer.sh build     build in Docker (arm64 Debian 13, like the device)
#   tools/cardputer.sh deploy    copy the program and HAL Zero's files to the device (as pi)
#   tools/cardputer.sh run       start it full-screen in the device's session (Esc quits)
#   tools/cardputer.sh shot      save a screenshot of what it's showing
# HOST defaults to pi@cardputerzero.local.
set -euo pipefail
cd "$(dirname "$0")/.."
HOST="${HOST:-pi@cardputerzero.local}"
case "${1:-}" in
  build)
    docker image inspect hal-zero-build >/dev/null 2>&1 || docker build -q -t hal-zero-build tools/cardputer >/dev/null
    docker run --rm -v "$PWD":/src -v hal-zero-pio:/opt/pio-core hal-zero-build pio run -e cardputer
    file .pio/build/cardputer/program
    ;;
  deploy)
    ssh "$HOST" 'mkdir -p ~/hal-zero ~/hal/shots'
    scp -q .pio/build/cardputer/program "$HOST":hal-zero/hal9000
    echo "deployed to $HOST:~/hal-zero/hal9000"
    ;;
  run)
    ssh -t "$HOST" 'export XDG_RUNTIME_DIR=/run/user/$(id -u); export WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-wayland-0}; ~/hal-zero/hal9000 --fullscreen "$@"'
    ;;
  shot)
    ssh "$HOST" 'pkill -USR1 -f hal-zero/hal9000 && sleep 1 && ls -t ~/hal/shots | head -1'
    ;;
  *) sed -n 2,8p "$0"; exit 1 ;;
esac

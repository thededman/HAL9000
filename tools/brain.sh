#!/usr/bin/env bash
# HAL's AI brain, on this computer (see docs/SETUP.md, "The AI brain").
#   tools/brain.sh start              start it in the background (log: brain/.run/brain.log)
#   tools/brain.sh stop
#   tools/brain.sh status
#   tools/brain.sh ask zero "What do you think of the old model?"    (or: ask desk "...")
#   tools/brain.sh pair [user@host]   share a key with HAL Zero and tell him where the brain is
#                                     (default pi@cardputerzero.local)
#   tools/brain.sh autostart on|off   start it by itself whenever you log in (macOS); no word: is it on?
set -euo pipefail
cd "$(dirname "$0")/.."
PY=tools/.venv/bin/python
RUN=brain/.run
[ -x "$PY" ] || { echo "Set up the voice tool first: docs/SETUP.md, step 3."; exit 1; }
port() { "$PY" -c 'from brain.config import port_from, read_env; print(port_from(read_env()))'; }
# Is the brain we started still running? (A pid left over from before a restart may belong to
# some other program by now: only a process that is the brain's server counts.)
running() {
  [ -f "$RUN/pid" ] || return 1
  ps -p "$(cat "$RUN/pid")" -o command= 2>/dev/null | grep -q "brain.server"
}
healthy() { curl -fs -m 1 "http://127.0.0.1:$1/health" >/dev/null 2>&1; }
case "${1:-}" in
  start)
    if running; then echo "already running (pid $(cat "$RUN/pid"))"; exit 0; fi
    mkdir -p "$RUN"
    if [ -f "$RUN/brain.log" ] && [ "$(wc -c < "$RUN/brain.log")" -gt 1048576 ]; then mv -f "$RUN/brain.log" "$RUN/brain.log.old"; fi
    p=$(port)
    # Something else on the port (a brain started by hand, or from another copy of this folder)
    # would make ours look started and then die: say so instead.
    if healthy "$p"; then echo "Something is already answering on port $p: stop it first (or set HAL_BRAIN_PORT in brain/.env)."; exit 1; fi
    nohup "$PY" -m brain.server >> "$RUN/brain.log" 2>&1 &
    echo $! > "$RUN/pid"
    for _ in $(seq 100); do
      running || break
      if healthy "$p"; then grep "listening" "$RUN/brain.log" | tail -n 1; exit 0; fi
      sleep 0.2
    done
    echo "HAL's brain didn't start. The end of brain/.run/brain.log:"; tail -n 5 "$RUN/brain.log" | cut -c1-300
    running && kill "$(cat "$RUN/pid")" 2>/dev/null
    rm -f "$RUN/pid"; exit 1
    ;;
  stop)
    if ! running; then echo "not running"; rm -f "$RUN/pid"; exit 0; fi
    kill "$(cat "$RUN/pid")"
    for _ in $(seq 50); do running || break; sleep 0.1; done
    rm -f "$RUN/pid"; echo "stopped"
    ;;
  status)
    if running && healthy "$(port)"; then echo "running (pid $(cat "$RUN/pid")), port $(port)"; else echo "not running"; exit 1; fi
    ;;
  ask)  shift; exec "$PY" -m brain.ask "$@" ;;
  pair) shift; exec "$PY" -m brain.pair "$@" ;;
  autostart)
    # After a restart nothing starts the brain, and HAL Zero says his higher functions are offline.
    # On macOS a login item (a LaunchAgent) can run "tools/brain.sh start" at every login.
    here=$(pwd)
    if [ "$(uname)" != Darwin ]; then
      echo "autostart is for macOS. On Linux, add this to your desktop's startup programs: $here/tools/brain.sh start"
      exit 1
    fi
    plist="$HOME/Library/LaunchAgents/local.hal9000.brain.plist"
    xml() { printf '%s' "$1" | sed -e 's/&/\&amp;/g' -e 's/</\&lt;/g' -e 's/>/\&gt;/g'; }
    # The login item: runs start once and lets the brain it started carry on (AbandonProcessGroup).
    # Your PATH goes along, so the brain finds whisper-cli and ffmpeg as it does from a terminal.
    item() {
      cat <<ITEM
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key><string>local.hal9000.brain</string>
  <key>ProgramArguments</key>
  <array><string>/bin/bash</string><string>$(xml "$here/tools/brain.sh")</string><string>start</string></array>
  <key>WorkingDirectory</key><string>$(xml "$here")</string>
  <key>EnvironmentVariables</key><dict><key>PATH</key><string>$(xml "$PATH")</string></dict>
  <key>RunAtLoad</key><true/>
  <key>AbandonProcessGroup</key><true/>
  <key>StandardOutPath</key><string>$(xml "$here/$RUN/autostart.log")</string>
  <key>StandardErrorPath</key><string>$(xml "$here/$RUN/autostart.log")</string>
</dict>
</plist>
ITEM
    }
    case "${2:-}" in
      on)
        mkdir -p "$HOME/Library/LaunchAgents" "$RUN"
        launchctl bootout "gui/$(id -u)" "$plist" 2>/dev/null || true
        item > "$plist"
        launchctl bootstrap "gui/$(id -u)" "$plist"
        echo "HAL's brain will start whenever you log in (it is starting now if it wasn't running)."
        echo "Undo with: tools/brain.sh autostart off"
        ;;
      off)
        launchctl bootout "gui/$(id -u)" "$plist" 2>/dev/null || true
        rm -f "$plist"
        echo "HAL's brain no longer starts by itself. (If it is running now, it carries on: tools/brain.sh stop)"
        ;;
      print) item ;;  # what "on" would install
      "") if [ -f "$plist" ]; then echo "on"; else echo "off"; fi ;;
      *) echo "tools/brain.sh autostart on|off"; exit 1 ;;
    esac
    ;;
  *) sed -n 2,9p "$0"; exit 1 ;;
esac

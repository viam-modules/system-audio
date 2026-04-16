#!/bin/bash
# Entrypoint for the system-audio module.
#
# On macOS, when running as root under a launchd daemon (i.e. viam-server was
# launched by launchd), delegates to darwin_tcc.sh which re-launches the
# binary as a LaunchAgent in the console user's GUI session for TCC microphone access.
# In all other cases, exec the binary directly.

echo "run.sh: running as $(whoami) (uid=$(id -u))"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MODULE_BIN="$SCRIPT_DIR/audio-module"

if [ "$(uname)" = "Darwin" ] && [ "$(id -u)" -eq 0 ]; then
    # Walk up the process tree to detect if any ancestor is launchd (PID 1).
    # viam-agent launches viam-server, so the chain may be several levels deep:
    # launchd (1) → viam-agent → viam-server → run.sh
    ANCESTOR_PID="$PPID"
    LAUNCHED_BY_LAUNCHD=0
    while [ "$ANCESTOR_PID" -gt 1 ] 2>/dev/null; do
        ANCESTOR_PID=$(ps -p "$ANCESTOR_PID" -o ppid= 2>/dev/null | tr -d ' ')
        if [ "$ANCESTOR_PID" -eq 1 ] 2>/dev/null; then
            LAUNCHED_BY_LAUNCHD=1
            break
        fi
    done
    if [ "$LAUNCHED_BY_LAUNCHD" -eq 1 ]; then
        echo "run.sh: detected launchd daemon context, delegating to darwin_tcc.sh"
        exec "$SCRIPT_DIR/darwin_tcc.sh" "$MODULE_BIN" "$@"
    else
        echo "run.sh: no launchd daemon context detected, executing binary directly"
    fi
fi

exec "$MODULE_BIN" "$@"

#!/bin/bash
# Entrypoint for the system-audio module.
#
# On macOS, when running as root (uid=0), delegates to darwin_tcc.sh which re-launches
# the binary as a LaunchAgent in the console user's GUI session for TCC microphone access.
# In all other cases, exec the binary directly.
set -e
echo "run.sh: running as $(whoami) (uid=$(id -u))"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MODULE_BIN="$SCRIPT_DIR/audio-module"

if [ "$(uname)" = "Darwin" ] && [ "$(id -u)" -eq 0 ]; then
    echo "run.sh: detected root context, delegating to darwin_tcc.sh"
    exec "$SCRIPT_DIR/darwin_tcc.sh" "$MODULE_BIN" "$@"
elif [ "$(uname)" = "Darwin" ]; then
    echo "run.sh: darwin but not in root context, executing binary directly"
fi

exec "$MODULE_BIN" "$@"

#!/bin/bash
# macOS: root can't access microphone due to TCC restrictions
# Switch to the console user (whoever is logged in)
# Linux: running as root is fine

echo "run.sh: running as $(whoami) (uid=$(id -u))" >&2

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MODULE_BIN="$SCRIPT_DIR/audio-module"

if [ "$(uname)" = "Darwin" ] && [ "$(id -u)" -eq 0 ]; then
    CONSOLE_USER=$(stat -f '%Su' /dev/console)

    if [ -n "$CONSOLE_USER" ] && [ "$CONSOLE_USER" != "root" ]; then
        # Transfer ownership to console user so they can access files
        chown -R "$CONSOLE_USER" "$SCRIPT_DIR"
        chown "$CONSOLE_USER" "$SCRIPT_DIR/.."

        exec su "$CONSOLE_USER" -c "\"$MODULE_BIN\" $*"
    else
        echo "run.sh: WARNING: Running as root on macOS. Microphone component will not work due to TCC restrictions." >&2
    fi
fi

exec "$MODULE_BIN" "$@"

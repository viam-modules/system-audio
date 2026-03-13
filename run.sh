#!/bin/bash
# macOS: root can't access microphone due to TCC restrictions
# Switch to the console user (whoever is logged in)
# Linux: running as root is fine

echo "run.sh: running as $(whoami) (uid=$(id -u))" >&1

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MODULE_BIN="$SCRIPT_DIR/audio-module"

if [ "$(uname)" = "Darwin" ] && [ "$(id -u)" -eq 0 ]; then
    CONSOLE_USER=$(stat -f '%Su' /dev/console)

    if [ -n "$CONSOLE_USER" ] && [ "$CONSOLE_USER" != "root" ]; then
        # Copy binary to /tmp so the console user can traverse the path.
        # The module may be installed under /var/root/ (drwx------), which
        # the console user cannot traverse even if they own the binary.
        TMPBIN=$(mktemp /tmp/audio-module-XXXXXX)
        cp "$MODULE_BIN" "$TMPBIN"
        chmod 755 "$TMPBIN"

        exec sudo -u "$CONSOLE_USER" "$TMPBIN" "$@"
    else
        echo "run.sh: WARNING: Running as root on macOS. Microphone component will not work due to TCC restrictions." >&2
    fi
fi

exec "$MODULE_BIN" "$@"

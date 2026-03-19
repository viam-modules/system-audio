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
        # Use VIAM_MACHINE_PART_ID for a stable path across restarts (avoids
        # accumulation in restart loops) that is still unique per robot instance.
        # If VIAM_MACHINE_PART_ID is not set, the TMPBIN will be /tmp/viam-audio-module-default
        TMPBIN="/tmp/viam-audio-module-${VIAM_MACHINE_PART_ID:-default}"
        echo "run.sh: Copying module to $TMPBIN" >&1
        cp "$MODULE_BIN" "$TMPBIN"
        chmod 755 "$TMPBIN"

        echo "run.sh: Executing viam-audio-module located at $TMPBIN as $CONSOLE_USER" >&1
        exec sudo -u "$CONSOLE_USER" "$TMPBIN" "$@"
    else
        echo "run.sh: WARNING: Running as root on macOS. Microphone component will not work due to TCC restrictions." >&2
    fi
fi

exec "$MODULE_BIN" "$@"

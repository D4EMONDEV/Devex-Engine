#!/bin/sh
# Plays a game exported for Linux for a few seconds without a screen, on lavapipe (the Vulkan of Mesa
# that draws on the processor) and SDL's offscreen video driver, and checks that it starts: the
# libraries it needs are found, its startup scene plays, nothing is logged as an error and it does not
# crash. Run on a bare system, it shows what a game needs from the machine of its players: the CI runs
# it in a container of Ubuntu with nothing but the Vulkan loader and Mesa.
#
#   scripts/play_exported_game.sh <export folder> [seconds]
set -eu

if [ $# -lt 1 ] || [ ! -d "$1" ]; then
    echo "usage: $0 <export folder> [seconds]" >&2
    exit 2
fi
folder=$(cd "$1" && pwd)
seconds=${2:-30}

# The game is the executable named as its package.
package=$(find "$folder" -maxdepth 1 -name "*.dvxpak" | head -n 1)
if [ -z "$package" ]; then
    echo "No package in $folder" >&2
    exit 2
fi
game=${package%.dvxpak}
if [ ! -x "$game" ]; then
    echo "No game beside $package" >&2
    exit 2
fi

echo "== Libraries the game takes from the system"
ldd "$game" "$folder"/*.so 2>&1 | grep "=>" | grep -v "$folder" | sed 's/ (0x[0-9a-f]*)//' | sort -u
unresolved=$(ldd "$game" "$folder"/*.so 2>&1 | grep "not found" || true)
if [ -n "$unresolved" ]; then
    echo "The game misses libraries of the system:" >&2
    echo "$unresolved" | sort -u >&2
    exit 1
fi

lavapipe=$(find /usr/share/vulkan/icd.d -name "lvp_icd*.json" 2>/dev/null | head -n 1)
if [ -z "$lavapipe" ]; then
    echo "lavapipe is not installed: the package mesa-vulkan-drivers brings it" >&2
    exit 2
fi

echo "== The game, for $seconds seconds"
log=$(mktemp)
status=0
SDL_VIDEO_DRIVER=offscreen VK_DRIVER_FILES=$lavapipe timeout "$seconds" "$game" > "$log" 2>&1 || status=$?
cat "$log"

# timeout ends a game still playing with 124.
failed=0
if [ "$status" -ne 124 ]; then
    echo "The game ended by itself, with $status, before $seconds seconds" >&2
    failed=1
fi
if ! grep -q "Playing " "$log"; then
    echo "The game did not play its startup scene" >&2
    failed=1
fi
if grep -E "^\[ *[0-9.]+\] (error|fatal) " "$log" > /dev/null || grep -q "The process crashed" "$log"; then
    echo "The game reported errors" >&2
    failed=1
fi
rm -f "$log"
if [ "$failed" -ne 0 ]; then
    exit 1
fi
echo "The game played for $seconds seconds."

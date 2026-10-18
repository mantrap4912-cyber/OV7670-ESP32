#!/usr/bin/env bash
#
# Syntax-check the library against the real ESP32 toolchain, without needing
# arduino-cli or a full sketch build. Useful for a fast local loop: it catches
# essentially every compile error in a second or two per file.
#
# It drives the compiler that the Arduino ESP32 core already installed, using
# the core's own flag files, so the include paths and defines match what a real
# build would use.
#
#   ./tools/syntax_check.sh            # library sources, esp32 target
#   ./tools/syntax_check.sh esp32s3    # library sources, esp32s3 target
#   ./tools/syntax_check.sh esp32 --examples
#
# Set ARDUINO15 if your Arduino data directory is somewhere unusual.
#
# Part of the OV7670-ESP32-NonFIFO library. MIT licensed.

set -u

TARGET="${1:-esp32}"
WANT_EXAMPLES="${2:-}"

case "$(uname -s)" in
    Darwin)          DEFAULT_A15="$HOME/Library/Arduino15" ;;
    MINGW*|MSYS*|CYGWIN*) DEFAULT_A15="$LOCALAPPDATA/Arduino15" ;;
    *)               DEFAULT_A15="$HOME/.arduino15" ;;
esac
A15="${ARDUINO15:-$DEFAULT_A15}"

PKG="$A15/packages/esp32"
if [ ! -d "$PKG" ]; then
    echo "error: ESP32 core not found under $PKG" >&2
    echo "install it through the Arduino IDE, or set ARDUINO15" >&2
    exit 1
fi

# Newest installed version of each component.
newest() { ls -1 "$1" 2>/dev/null | sort -V | tail -1; }

CORE_VER="$(newest "$PKG/hardware/esp32")"
CORE="$PKG/hardware/esp32/$CORE_VER"

case "$TARGET" in
    esp32)   LIBDIR="esp32-libs";   GXX_NAME="xtensa-esp32-elf-g++" ;;
    esp32s3) LIBDIR="esp32s3-libs"; GXX_NAME="xtensa-esp32s3-elf-g++" ;;
    *) echo "error: unsupported target '$TARGET' (esp32 | esp32s3)" >&2; exit 1 ;;
esac

LIB_VER="$(newest "$PKG/tools/$LIBDIR")"
B="$PKG/tools/$LIBDIR/$LIB_VER"
if [ ! -d "$B" ]; then
    echo "error: no prebuilt libs for $TARGET under $PKG/tools/$LIBDIR" >&2
    exit 1
fi

# The compiler lives in whichever toolchain package is installed.
GXX=""
for cand in "$PKG/tools"/*/*/bin/"$GXX_NAME" "$PKG/tools"/*/*/bin/"$GXX_NAME.exe"; do
    [ -x "$cand" ] && { GXX="$cand"; break; }
done
if [ -z "$GXX" ]; then
    echo "error: $GXX_NAME not found under $PKG/tools" >&2
    exit 1
fi

# sdkconfig.h ships once per flash-mode variant; any of them will do for a
# syntax check.
SDKCFG=""
for d in "$B"/*/include/sdkconfig.h; do
    [ -f "$d" ] && { SDKCFG="$(dirname "$d")"; break; }
done

EXTRA=""
[ "$TARGET" = "esp32s3" ] && EXTRA="-DOV7670_ENABLE_LCDCAM_EXPERIMENTAL"

VARIANT="$CORE/variants/$TARGET"
[ -d "$VARIANT" ] || VARIANT="$CORE/variants/esp32"

LIB_INCLUDES=""
for d in "$CORE/libraries"/*/src; do
    [ -d "$d" ] && LIB_INCLUDES="$LIB_INCLUDES -I$d"
done

echo "target      : $TARGET"
echo "core        : $CORE_VER"
echo "prebuilt    : $LIBDIR/$LIB_VER"
echo "compiler    : $(basename "$GXX")"
echo

check() {
    local file="$1"; shift
    printf '%-40s' "$(basename "$(dirname "$file")")/$(basename "$file")"
    if out=$("$GXX" -fsyntax-only \
            @"$B/flags/cpp_flags" @"$B/flags/defines" \
            ${SDKCFG:+-I"$SDKCFG"} \
            -iprefix "$B/include/" @"$B/flags/includes" \
            -DARDUINO=10812 -DARDUINO_ARCH_ESP32 -DESP32 -DF_CPU=240000000L \
            -I"$CORE/cores/esp32" -I"$VARIANT" $LIB_INCLUDES \
            -Isrc $EXTRA "$@" "$file" 2>&1); then
        echo "OK"
    else
        echo "FAIL"
        echo "$out" | grep -E 'error:' | head -8
        FAILED=$((FAILED + 1))
    fi
}

FAILED=0
cd "$(dirname "$0")/.."

echo "--- library sources ---"
for f in src/*.cpp; do check "$f"; done

if [ "$WANT_EXAMPLES" = "--examples" ]; then
    echo
    echo "--- examples ---"
    for d in examples/*/; do
        n="$(basename "$d")"
        [ -f "$d$n.ino" ] && check "$d$n.ino" -include Arduino.h -x c++
    done
fi

echo
if [ "$FAILED" -eq 0 ]; then
    echo "all clean"
else
    echo "$FAILED file(s) failed"
fi
exit "$FAILED"

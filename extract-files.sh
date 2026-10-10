#!/bin/sh
set -eu

# Usage: extract-files.sh [system-directory | adb]
# A system directory may be an unpacked ROM or the contents of /system.
SOURCE=${1:-"$HOME/Android/system"}
if [ "$SOURCE" != adb ]; then
    SOURCE=$(cd "$SOURCE" && pwd)
fi
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$SCRIPT_DIR"
BASE=../../../vendor/lge/batman_lgu/proprietary
mkdir -p "$BASE"
STAGE=$(mktemp -d "${BASE}.extract.XXXXXX")
trap 'rm -rf "$STAGE"' EXIT
trap 'exit 1' HUP INT TERM

while IFS='|' read -r ENTRY EXPECTED_HASH; do
    case "$ENTRY" in
        ''|'#'*) continue ;;
    esac
    FILE=${ENTRY%%:*}
    mkdir -p "$STAGE/$(dirname "$FILE")"
    CANDIDATES=$FILE
    case "$ENTRY" in
        *:*) CANDIDATES="${ENTRY#*:} $FILE" ;;
    esac
    case "$FILE" in
        lib/egl/libEGL_adreno200.so)
            # Our installed EGL path is a compatibility wrapper. Extract the
            # original p930 blob from lib/, with the stock location as fallback.
            CANDIDATES="lib/libEGL_adreno200.so $FILE" ;;
        lib/egl/libGLESv2_adreno200.so)
            CANDIDATES="lib/libGLESv2_adreno200.so $FILE" ;;
        lib/hw/sensors.msm8660.so)
            CANDIDATES="${ENTRY#*:} lib/hw/sensors.vendor.msm8660.so $FILE" ;;
        lib/hw/camera.msm8660.so)
            CANDIDATES="lib/hw/camera.vendor.msm8660.so $FILE" ;;
        lib/libtime_genoff.so)
            CANDIDATES="vendor/lib/libtime_genoff.so $FILE" ;;
    esac
    FOUND=false
    for CANDIDATE in $CANDIDATES; do
        if [ "$SOURCE" = adb ]; then
            if ! adb pull "/system/$CANDIDATE" "$STAGE/$FILE"; then
                continue
            fi
        else
            [ -f "$SOURCE/$CANDIDATE" ] || continue
            cp "$SOURCE/$CANDIDATE" "$STAGE/$FILE"
        fi
        if [ -n "${EXPECTED_HASH:-}" ]; then
            ACTUAL_HASH=$(sha1sum "$STAGE/$FILE" | awk '{print $1}')
            if [ "$ACTUAL_HASH" != "$EXPECTED_HASH" ]; then
                echo "SHA-1 mismatch for $CANDIDATE" >&2
                echo "  expected: $EXPECTED_HASH" >&2
                echo "  actual:   $ACTUAL_HASH" >&2
                continue
            fi
        fi
        FOUND=true
        break
    done
    if [ "$FOUND" != true ]; then
        echo "No matching source for $FILE; vendor files were not changed" >&2
        exit 1
    fi
done < proprietary-files.txt

# Apply the reproducible camera poll initialization fix after stock hash checks.
python3 "$SCRIPT_DIR/tools/fix-camera-poll.py" "$STAGE/lib/liboemcamera.so"

# Convert verified legacy CRT relocations after all stock hash checks.
python3 "$SCRIPT_DIR/tools/fix-textrel.py" --apply "$STAGE"
python3 "$SCRIPT_DIR/tools/fix-textrel-extra.py" --apply "$STAGE"

# Do not replace working blobs until every required file and hash is verified.
cp -a "$STAGE/." "$BASE/"
rm -f "$BASE/lib/egl/libplayback_adreno200.so"
./setup-makefiles.sh

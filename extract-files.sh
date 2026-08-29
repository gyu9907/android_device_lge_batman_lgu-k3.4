#!/bin/sh

VENDOR=lge
DEVICE=batman_lgu

BASE=../../../vendor/$VENDOR/$DEVICE/proprietary
rm -rf $BASE/*
hostname=`hostname`

while IFS='|' read -r FILE EXPECTED_HASH; do
    case "$FILE" in
        ''|'#'*) continue ;;
    esac
    DIR=`dirname $FILE`
    if [ ! -d $BASE/$DIR ]; then
        mkdir -p $BASE/$DIR
    fi
#    if [ "${hostname}" = "StoneColdSVR" ]; then
        cp ~/Android/system/$FILE $BASE/$FILE
#    else
#        adb pull /system/$FILE $BASE/$FILE
#    fi

    if [ -n "${EXPECTED_HASH:-}" ]; then
        ACTUAL_HASH=`sha1sum "$BASE/$FILE" | awk '{print $1}'`
        if [ "$ACTUAL_HASH" != "$EXPECTED_HASH" ]; then
            echo "SHA-1 mismatch for $FILE" >&2
            echo "  expected: $EXPECTED_HASH" >&2
            echo "  actual:   $ACTUAL_HASH" >&2
            exit 1
        fi
    fi
done < proprietary-files.txt

./setup-makefiles.sh

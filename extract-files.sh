#!/bin/sh

VENDOR=lge
DEVICE=batman_lgu

BASE=../../../vendor/$VENDOR/$DEVICE/proprietary
rm -rf $BASE/*
hostname=`hostname`

for FILE in `cat proprietary-files.txt | grep -v ^# | grep -v ^$`; do
    DIR=`dirname $FILE`
    if [ ! -d $BASE/$DIR ]; then
        mkdir -p $BASE/$DIR
    fi
#    if [ "${hostname}" = "StoneColdSVR" ]; then
        cp ~/Android/system/$FILE $BASE/$FILE
#    else
#        adb pull /system/$FILE $BASE/$FILE
#    fi
done

./setup-makefiles.sh

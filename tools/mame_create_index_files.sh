#!/usr/bin/bash
#  SPDX-License-Identifier: MIT
#
#  ES-DE
#  mame_create_index_files.sh
#
#  As input, this script takes the MAME driver information XML file from the official
#  MAME release and generates the files mamebioses.xml, mamedevices.xml and mamenames.xml.
#
#  There is not much error checking going on here, this script is not intended to be run
#  by the end user.
#
#  xmlstarlet must be installed or this script will fail.
#
#  Download the driver file from here:
#  https://www.mamedev.org/release.php
#  It's enough to download the driver information, not the complete emulator.
#
#  This script is only intended to be used on Linux systems.
#

if [ $# -ne 1 ]; then
  echo "Usage: ./mame_create_index_files.sh <MAME driver file>"
  echo "For example:"
  echo "./mame_create_index_files.sh mame0221.xml"
  exit
fi

if [ ! -f $1 ]; then
  echo "Can't find MAME driver file" $1
  exit
fi

MAME_XML_FILE=$1
MAMEBIOSFILE=mamebioses.xml
MAMEDEVICEFILE=mamedevices.xml
MAMENAMEFILE=mamenames.xml

echo "<!-- Last updated with information from MAME driver file" $1 "-->" > $MAMEBIOSFILE

for bios in $(xmlstarlet sel -t -m "/mame/machine[@isbios=\"yes\"]" -v "@name" \
    -n $MAME_XML_FILE); do
  echo "<bios>"${bios}"</bios>" >> $MAMEBIOSFILE
done

echo "<!-- Last updated with information from MAME driver file" $1 "-->" > $MAMEDEVICEFILE

for device in $(xmlstarlet sel -t -m "/mame/machine[@isdevice=\"yes\"][rom]" \
    -v "@name" -n $MAME_XML_FILE); do
  echo "<device>"${device}"</device>" >> $MAMEDEVICEFILE
done

echo "<!-- Generated from MAME driver file" $1 "-->" > $MAMENAMEFILE

# games247: clones get a cloneof entry with the name of their parent set, which the game
# lists use to show each game once and offer its other versions when it's started.
xmlstarlet sel -t -m "/mame/machine[not(@isbios=\"yes\")][not(@isdevice=\"yes\")][rom]" \
    -o "<game>" -n -o "	<mamename>" -v "@name" -o "</mamename>" -n \
    -o "	<realname>" -v description -o "</realname>" -n \
    --if "@cloneof" -o "	<cloneof>" -v "@cloneof" -o "</cloneof>" -n --break \
    -o "</game>" -n $MAME_XML_FILE >> $MAMENAMEFILE

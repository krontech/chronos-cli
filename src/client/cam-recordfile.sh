#!/bin/sh
NAME=$(basename $0)

## Defaults
FORMAT="h264"
BITRATE=40000000
FRAMERATE=60
START=0
LENGTH=0

show_help() {
cat << EOF
Usage: $NAME [options] FILE

Record a segment of video to a file.

options:
   --start NUM     Begin the recording at frame number NUM
   --length LEN    Continue the recording for LEN frames
   --framerate FPS Encode video at FPS frames per second
   --bitrate BPS   Encode compressed video with BPS bits per second
   --h264          Encode compressed video with the h.264 codec.
   --dng           Save uncompressed raw image in CinemaDNG format.
   --tiff          Save processed RGB images in Adobe TIFF format.
   --raw16         Save uncompressed raw images in 16-bit binary format.
   --raw12         Save uncompressed raw images in 12-bit packed binary format.
EOF
}

## Parse Arguments
OPTS=$(getopt \
   --longoptions start:,length:,bitrate:,framerate:,h264,dng,tiff,raw16,raw12,help \
   --options h \
   --name "$NAME" \
   -- $@)

eval set --$OPTS

while [[ $# -gt 0 ]]; do
   case "$1" in
      --start)
         START="$2"
         shift 2
         ;;

      --length)
         LENGTH="$2"
         shift 2
         ;;

      --bitrate)
         BITRATE="$2"
         shift 2
         ;;

      --framerate)
         FRAMERATE="$2"
         shift 2
         ;;

      --h264)
         FORMAT="h264"
         shift 1
         ;;

      --dng)
         FORMAT="dng"
         shift 1
         ;;

      --tiff)
         FORMAT="tiff"
         shift 1
         ;;

      --help|-h)
         show_help
         shift 1
         exit 0
         ;;

      --)
         if [[ $# -le 1 ]]; then
             echo "$(basename $0): Missing argument FILENAME" >&2
             exit 1
         fi
         FILENAME="$2"
         shift 2
         break
         ;;

      *)
         break
         exit 1
         ;;
   esac
done

## Build the JSON to start a recording.
mkjson() {
   echo "{"
   echo "   \"format\": \"$FORMAT\","
   echo "   \"start\": $START,"
   if [[ "$LENGTH" -gt 0 ]]; then
      echo "   \"length\": $LENGTH,"
   fi
   if [[ "$FORMAT" == "h264" ]]; then
      echo "   \"bitrate\": $BITRATE,"
      echo "   \"framerate\": $FRAMERATE,"
   fi
   echo "   \"filename\": \"$FILENAME\""
   echo "}"   
}

## Do the recording
mkjson | /opt/camera/cam-json -v recordfile -


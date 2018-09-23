Video Pipeline Interface
========================

The `cam-pipeline` program is responsible for managing the multimedia pipeline
of the camera. This program connects the video stream from the FPGA to the
output output devices on the camera. This pipeline can operate in one of three
modes at any given time: live display, video playback, and video recording.

In live display mode, the FPGA is capturing data off the image sensor, and will
stream frames out to the camera at 60fps. When in this mode, the pipeline will
split the incoming video and scale each stream to fit their respective output
device (LCD, HDMI and RTSP streams).

In playback mode, the FPGA is replaying video from its internal memory buffer
and the pipeline daemon is given control of the playback position and framerate
of the video stream. Upon entering playback mode, the video will be paused on
the first frame in memory, but the playback rate and position can be cahnged
using the `playback` function.

In filesave mode, the FPGA replays video in the same manner as playback mode,
but video stream will instead be passed through an encoder element and will be
written to a file rather than the output devices. When the saving is complete,
the pipeline will generate an EOF signaland return to playback mode.

The `cam-pipeline` program will respond to the following POSIX signals:

 * `SIGHUP`: Reboot the video pipeline and update its configuration.
 * `SIGINT`: Terminate the pipeline and shut down gracefully.
 * `SIGUSR1`: Seek one frame forward when in playback mode.
 * `SIGUSR2`: Seek one frame backward when in playback mode.

`SIGUSR1` can also be sent as a POSIX.1b signal via sigqueue(), in which case
the `sigval.si_int` parameter specifies the number of frames to seek when in
playback mode.

The `cam-pipeline` program will create a named FIFO at `/tmp/cam-screencap.jpg`,
and operates the write end of the FIFO. When this FIFO is opened, the pipeline
will encode the current frame as a JPEG and write it into the FIFO. Thus the
command `cat /tmp/cam-screencap.jpg > somefile.jpg` can be used to take a live
screenshot from the pipeline.

The DBus interface to the video pipeline daemon is accessible at
`/com/krontech/chronos/video` and conforms to the interface given by
[com.krontech.chronos.video.xml](../../src/api/com.krontech.chronos.video.xml),
which implements the methods:

* [`status`](#status): Return the status of the video pipeline.
* [`flush`](#flush): Clear recorded video and return to live display mode.
* [`playback`](#playback): Control the frame position and playback rate.
* [`configure`](#configure): Configure video settings.
* [`livedisplay`](#livedisplay): Switch to live display mode.
* [`recordfile`](#recordfile): Encode and write video to a file.
* [`stop`](#stop): Terminate video encoding and return to playback mode.
* [`overlay`](#overlay): Configure an overlay text box for video and frame information.
* `livestream`: Configure network video streams (TODO)

And emits the DBus signal:
 * [`sof`](#sof): Video recording has started and the pipeline has entered record mode.
 * [`eof`](#eof): Video recording is complete and the pipeline has exited record mode.

status
------
Returns the current status of the video pipeline, This method takes no
arguments, and the returned hash map will contain the following members.

| Output            | Type      | Description
|:----------------- |:--------- |:--------------
| `"apiVersion"`    | `string`  | `"1.0"` for all cameras implemeting this specification.
| `"playback"`      | `boolean` | `true` if the video pipeline is in playback mode.
| `"filesave"`      | `boolean` | `true` if the video pipeline is in file saving mode.
| `"position"`      | `uint`    | The current frame number being display while in playback or record mode.
| `"totalFrames"`   | `uint`    | The total number of frames across all recorded segments.
| `"segment"`       | `uint`    | The segment to which the current frame belongs.
| `"framerate"`     | `float`   | The target playback rate when in playback mode, or estimated frame rate when in record mode.

flush
-----
Clear all recording segments from video memory and return the video system back
to live display mode. This method takes no arguments, and returns the same values
as the [`status`](#status) method.

playback
--------
Sets the pipeline into playback mode and begins replaying frames from video memory out to the
output devices. If the pipeline is already in playback mode, this call will adjust the playback
rate and position. The `framerate` can be set to positive numbers to play the video foreward,
or negative to play backwards. A value of zero will pause the video. The caller can also specify
a `position` from which to begin playback, and a `loopcount` to limit playback to a subset of
the captured frames. This method takes the following optional arguments.

| Input             | Type      | Description
|:----------------- |:--------- |:--------------
| `"framerate"`     | `int`     | The frame rate to use in playback mode.
| `"position"`      | `uint`    | The frame number to start playback from.
| `"loopcount"`     | `uint`    | The number of frames to loop over.

This method will return the same values as the [`status`](#status) method.

configure
---------
Configure the video size and position to be rendered to the LCD interface, or
adjust video settings.

| Input             | Type      | Description
|:----------------- |:--------- |:--------------
| `"zebra"`         | `boolean` | Enable zebra strips for exposure aid.
| `"peaking"`       | `boolean` | Enable peaking for focus aid.
| `"hres"`          | `uint`    | Horizontal resolution of the video display area.
| `"vres"`          | `uint`    | Vertical resolution of the video display area.
| `"xoff"`          | `uint`    | Horizontal position of the video display area.
| `"yoff"`          | `uint`    | Vertical position of the video display area.

This method will return the same values as the [`status`](#status) method.

livedisplay
-----------
Switch the video system into live display mode. This method takes no input parameters, and
returns the same values as the [`status`](#status) method.

recordfile
----------
Select a range of frames to be encoded, and provide optional recording parameters. Upon calling
this method, the pipeline will enter record mode to write those frames to a file.

| Input             | Type      | Description
|:----------------- |:--------- |:--------------
| `"filename"`      | `string`  | The destination file or directory to be written.
| `"format"`        | `string`  | The encoding format to select.
| `"start"`         | `uint`    | The starting frame number of the recording region.
| `"length"`        | `uint`    | The number of frames to be recorded.
| `"framerate"`     | `uint`    | The desired framerate of the encoded video file, in frames per second.
| `"bitrate"`       | `uint`    | The maximum encoded bitrate for compressed formats, in bits per second.

The `format` field accepts a FOURCC code defining the output video format, supported values include:

| FOURCC Code           | Description
|:----------------------|:---------------------
| `"h264"` or `"x264"`  | H.264 compressed video saved in an MPEG-4 container.
| `"dng"`               | Directory of CinemaDNG files, containing the raw sensor data.
| `"tiff"`              | Directory of Adobe TIFF files, containing the processed RGB image.
| `"byr2"` or `"y16"`   | Raw sensor data padded to 16-bit little-endian encoding.
| `"y12b"`              | Raw sensor data in packed 12-bit little-endian encoding.
| `"refimg"`            | Take the average of 16 frames and store the result in `"y16"` encoding.

The `framerate` and `bitrate` fields are only used for H.264 compressed video formats, and are ignored
for all other encoding formats.

The `recordfile` function will immediately return an empty hash map.

stop
----
Terminate any active filesave events, and return to playback mode. In any other state this should cause the
video system to reboot and return to the same state. This method takes no parameters, and returns no values.

overlay
-------
Configure a text box to overlay ontop of the video in playback mode. The video overlay will also be coped
into the processed video formats `"tiff"` and `"h264"`.

| Input             | Type      | Description
|:----------------- |:--------- |:--------------
| `"format"`        | `string`  | The string to be written into the text box, including optional format specifiers.
| `"position"`      | `string`  | Where to position the text box within the video frame.
| `"textbox"`       | `string`  | The width and height of the text box to draw ontop of the video.
| `"justify"`       | `string`  | The alignment of text within the box.
| `"color"`         | `uint`    | The RGBA coordinate of the font color.

The string provided by the `"format"` parameter is written into the text box. If the `"format"` includes format
specifiers (subsequences beginning with `"%"`), the sequence is replaced with datum describing the frame. The
syntax of the format specifiers follows the `printf` function, and recognizes the following specifiers and their
encodings. The `flags`, `width`, and `precision` from the `printf` function sytax are supported to further
define the output string.

| Specifier     | Format Type       | Description
|:------------- |:----------------- |:--------------
| `%t`          | `unsigned long`   | Total number of frames recorded.
| `%e`          | `double`          | Frame exposure time in microseconds.
| `%f`          | `unsigned long`   | Current frame number.
| `%g`          | `unsigned long`   | Recording segment number.
| `%h`          | `unsigned long`   | Frame number within the recording segment.
| `%z`          | `unsigned long`   | Size of the recording segment.
| `%r`          | `double`          | Framerate.
| `%n`          | `long long`       | Nanoseconds since the trigger event.
| `%u`          | `long`            | Microseconds since the trigger event.
| `%m`          | `long`            | Milliseconds since the trigger event.
| `%U`          | `double`          | Microseconds since the trigger event.
| `%M`          | `double`          | Milliseconds since the trigger event.
| `%S`          | `double`          | Seconds since the trigger event.
| `%%`          | None              | Literal percent.

TODO: The `"justify"` parameter determines whether the text written to the box should be aligned to the
`"left"`, `"right"`, or `"center"` of the text box. If not otherwise defined, the default is to align
the text to the `"left"` side of the text box.

The `"position"` parameter defines the location of the textbox on the video. It can take the enumerated
vales of `"top"` and `"bottom"` to place the text box at either the top or bottom of the video with a
horizontal position of zero. Or it can be set to a string of the form `"HRESxVRES"` where `HRES` and `VRES`
are decimal integers containing the horizontal and vertical positions accordingly.

The `"textbox"` parameter defines the size the text box to be drawn ontop of the video. The string is of
the form `"WIDTHxHEIGHT"` where `WIDTH` defines the horizontal size of the box in pixels and `HEIGHT` is
the vertical size. A vertical height of zero will default to the minimum height to draw a single line of
text without clipping. A horizontal width of zero will extend the text box to use the maximum width of the
video frame.

sof
---
The `sof` DBus signal is emittted by the pipeline when video recording has started. Upon emitting the `sof`
the pipeline will begin replaying the selected video frames and encoding them to disk. Recording will continue
until the end of the selected video has been reached, or an error occurs. The `eof` signal will include a hash
map containing the same values as the [`status`](#status) method..

eof
---
The `eof` DBus signal is emitted by the pipeline when video recording has finished, upon emitting the `eof`,
the pipeline will send itself a `SIGHUP` to reconfigure the pipeline and return to either playback or live
display mode. The `eof` signal will include a hash map containing the same values as the [`status`](#status)
method.

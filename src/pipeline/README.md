Video Pipeline Daemon
=====================

The `cam-pipeline` program is responsible for managing the multimedia pipeline
of the camera. This program connects the video stream from the FPGA to the
output output devices on the camera. This pipeline can operate in one of three
modes at any given time: live display, video playback, and video file saving.

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
 * `SIGINT` or `SIGTERM`: Terminate the pipeline and shut down gracefully.
 * `SIGUSR1`: Seek one frame forward when in playback mode.
 * `SIGUSR2`: Seek to the first frame when in playback mode.

When sent as a POSIX.1b signal via sigqueue(), `SIGUSR1` is interpreted as a
seek relative to the current frame. In this case, the `sigval.si_int` can be
positive to seek forwards, or negative to seek backwards.

When sent as a POSIX.1b signal via sigqueue(), `SIGUSR2` is interpreted as a
seek to an abolute frame number. In this case, the `sigval.si_int` gives the
desired frame number.

The `cam-pipeline` program will create a named FIFO at `/tmp/cam-screencap.jpg`,
and operates the write end of the FIFO. When this FIFO is opened, the pipeline
will encode the current frame as a JPEG and write it into the FIFO. Thus the
command `cat /tmp/cam-screencap.jpg > somefile.jpg` can be used to take a live
screenshot from the pipeline.

Video D-Bus Methods
===================
The DBus interface to the video pipeline daemon is accessible at
`/ca/krontech/chronos/video` and conforms to the interface given by
[ca.krontech.chronos.video.xml](../../src/api/ca.krontech.chronos.video.xml),
which implements the methods:

| Method Name                   | Input Type | Description
|:------------------------------|:-----------|:-----------
| [`status`](#status)           |            | Return the status of the video pipeline.
| [`get`](#get)                 | `as`       | Retrieve the value of one or more parameters.
| [`set`](#set)                 | `a{sv}`    | Change the value of one or more parameters.
| [`flush`](#flush)             |            | Clear recorded video and return to live display mode.
| [`playback`](#playback)       | `a{sv}`    | Control the frame position and playback rate.
| [`configure`](#configure)     | `a{sv}`    | Configure video settings.
| [`livedisplay`](#livedisplay) | `a{sv}`    | Switch or configure live display mode.
| [`recordfile`](#recordfile)   | `a{sv}`    | Encode and write video to a file.
| [`stop`](#stop)               |            | Terminate video encoding and return to playback mode.
| [`overlay`](#overlay)         | `a{sv}`    | Configure an overlay text box for video and frame information.

And emits the DBus signal:
 * [`sof`](#sof): The video pipeline has changed mode, and the video stream has started.
 * [`eof`](#eof): The video stream has ended and the video pipeline is about to change mode.
 * [`segment`](#segment): The video pipeline has received a new video segment from the FPGA.
 * [`update`](#update): One or more parameters has been updated.

Methods which take arguments typically accept an array of string variant tuples (D-Bus type
code of `a{sv}`) that form a hash of the input arguments. All methods and signals return an
array of string variant tuples.

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
| `"totalSegments"` | `uint`    | The total number of segments recorded in memory.
| `"framerate"`     | `float`   | The target playback rate when in playback mode, or estimated frame rate when in record mode.
| `"error"`         | `string`  | A description of the error (only present when generated in response to an error).
| `"filename"`      | `string`  | The name of the file being saved (only present if `"filesave"` is `true`).

get
---
This method takes as input an array of strings, naming the parameters to be retrieived
from the pipeline and returns a hash map containing the current parameter values.

set
---
This method takes as input a hash map with the parameter values to be configured on
the video system.

flush
-----
Clear all recording segments from video memory and return the video system back
to live display mode.

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

configure
---------
Configure the video size and position to be rendered to the LCD interface, or
adjust video settings.

| Input             | Type      | Description
|:----------------- |:--------- |:--------------
| `"zebra"`         | `boolean` | Enable zebra strips for exposure aid.
| `"peaking"`       | variable  | Enable peaking for focus aid.
| `"hres"`          | `uint`    | Horizontal resolution of the video display area.
| `"vres"`          | `uint`    | Vertical resolution of the video display area.
| `"xoff"`          | `uint`    | Horizontal position of the video display area.
| `"yoff"`          | `uint`    | Vertical position of the video display area.

The `peaking` field can accept several types; if a `boolean` type is used a
value of `true` then it will enable focus peaking with the default color (cyan),
or the `peaking` field accepts one of the following strings to select the focus
peaking color:
 * `"red"`
 * `"green"`
 * `"blue"`
 * `"cyan"`
 * `"magenta"`
 * `"yellow"`
 * `"white"`

If the `peaking` field is present with any other type, or value, focus peaking will
be disabled.

livedisplay
-----------
Switches the pipeline to live display mode, and optionally reconfigures the
video resolution to receive from the FPGA. If both `hres` and `vres` are zero,
this is interpreted as a request to switch to live display without any
configuration changes.

| Input             | Type      | Description
|:----------------- |:--------- |:--------------
| `"zebra"`         | `boolean` | Enable zebra strips for exposure aid.
| `"peaking"`       | variable  | Enable peaking for focus aid.
| `"hres"`          | `int`     | The horizontal resolution of live video.
| `"vres"`          | `uint`    | The vertical resolution of live video.
| `"cropx"`         | `uint`    | The horizontal video crop size (default: 0).
| `"cropy"`         | `uint`    | The vertical video crop size (defualt: 0).
| `"startx"`        | `uint`    | The horizontal starting offset for cropped video (default: 0).
| `"starty"`        | `uint`    | The vertical starting offset for cropped video (defualt: 0).

If the `cropx` or `cropy` parameters are set to non-zero values, the live video stream
will be cropped before entering the scaler elements, otherwise the full video size
will be rescaled to fit the LCD output size set by the [`configure`](#configure) method.

For a description of the acceptable values provided to the `peaking` field,
refer to the [`configure`](#configure) method.

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

The `format` field accepts a string to enumerate the output video format, supported values include:

| Format                | Description
|:----------------------|:---------------------
| `"h264"` or `"x264"`  | H.264 compressed video saved in an MPEG-4 container.
| `"dng"`               | Directory of CinemaDNG files, containing the raw sensor data.
| `"tiff"`              | Directory of Adobe TIFF files, containing the processed RGB image.
| `"tiffraw"`           | Directory of 16-bit TIFF files containing the raw sensor data.
| `"byr2"` or `"y16"`   | Raw sensor data padded to 16-bit little-endian encoding.
| `"y12b"`              | Raw sensor data in packed 12-bit little-endian encoding.

The `framerate` and `bitrate` fields are only used for H.264 compressed video formats, and are ignored
for all other encoding formats.

stop
----
Terminate any active filesave events, and return to playback mode. In any other state this should cause the
video system to reboot and return to the same state.

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

Video D-Bus Signals
===================
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

segment
-------
The `segment` DBus signal is emitted by the pipeline when a new segment of recorded video is received from
the FPGA. The `segment` signal will include a hash map containing the same values as the [`status`](#status)
method.

notify
------
The `notify` DBus signal is emitted by the pipeline when one or more parameters have been updated. The
`segments` signal will include a hash map containing the new parameter values.

Video D-Bus Properties
======================

Each parameter is marked with the following flags:

 * `G`: The parameter's current value can be queried via the `get` command.
 * `S`: The parameter's value can be updated via the `set` command.
 * `U`: Changes to the parameter's value will be reported via the `update` signal.
 * `x`: The parameter is planned, but not yet implemented.

Each parameter also defines a type as follows:

| API Type | D-Bus Signatures   | Python Types | Description
|:---------|:-------------------|:-------------|:-----------
| `bool`   | `b`                | `boolean`    | Either `true` or `false`
| `float`  | `t`                | `float`      | Floating-point number.
| `int`    | `i`                | `int`        | Integer type, supporting up to 32-bit precision.
| `enum`   | `i`                | `int`        | The description of each type must specify the allowed values.
| `array`  | `ad`               | `list`       | An array of floating point values.
| `string` | `s`                | `str`        | A character string, which should support UTF-8 encoding.
| `dict`   | `a{sv}`            | `dict`       | An array of name/value pairs. Values may contain any type (including another `dict`).

The available parameters which can be accessed by the [`get`](#get) and [`set`](#set)
a methods are as follows:

| Parameter         | G | S | U | Type   | Description
|:----------------- |:--|:--|:--|:-------|:-----------
| `overlayEnable`   |`G`|`S`|`x`| bool   |
| `overlayFormat`   |`G`|`S`|`x`| string | A `printf`-style format string to set the overlay text.
| `zebraLevel`      |`G`|`S`|`x`| float  | Zebra stripe sensitivity.
| `focusPeakLevel`  |`G`|`S`|`x`| float  | Focus peaking edge detection sensitivity (in the rante of 0 to 1.0).
| `focusPeakColor`  |`G`|`S`|`x`| enum   | One of Red, Green, Blue, Cyan, Magenta, Yellow, White or Disabled.
| `videoState`      |`G`|   |`x`| enum   | One of `paused`, `live`, `playback` or `filesave`
| `playbackRate`    |`G`|`S`|`x`| int    | Framerate that live video will be played back when in `playback`.
| `playbackPosition`|`G`|`S`|   | int    | Current frame number being displayed when in `playback`.
| `playbackStart`   |`G`|`S`|`x`| int    | Starting frame number to display when entering `playback`.
| `playbackLength`  |`G`|`S`|`x`| int    | Number of frames to play when in `playback` before loooping back to `playbackStart`.
| `totalFrames`     |`G`|   |`x`| int    | Total number of frame captured in the camera's memory.
| `totalSegments`   |`G`|   |`x`| int    | Total number of recording segments captured in the camera's memory.

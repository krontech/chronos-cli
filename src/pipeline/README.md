Video Pipeline Interface
========================

The `cam-pipeline` program is responsible for managing the multimedia pipeline
of the camera. This program connects the video stream from the FPGA to the output
output devices on the camera. This pipeline can operate in one of three modes
at any given time: live display, video playback, and video recording.

In live display mode, the FPGA is capturing data off the image sensor, and will
stream frames out to the camera at 60fps. When in this mode, the pipeline will
split the incoming video and scale each stream to fit their respective output
device (LCD, HDMI and RTSP streams).

In playback mode, the FPGA is replaying video from its internal memory buffer and
the pipeline daemon is given control of the playback position and framerate of the
video stream. Upon entering playback mode, the video will be paused on the first
frame in memory, but the playback rate and position can be cahnged using the
`playback` function.

In record mode, the FPGA replays video in the same manner as playback mode, but
video stream will instead be passed through an encoder element and will be written
to a file rather than the output devices. When the recording is complete, the
pipeline will send itself a SIGHUP to return to live display or playback mode.

The `cam-pipeline` program will respond to the following POSIX signals:

 * `SIGHUP`: Reboot the video pipeline and update its configuration.
 * `SIGINT`: Terminate the pipeline and shut down gracefully.
 * `SIGUSR1`: Reload recording segment data from the FPGA.
 * `SIGUSR2`: Disable the video scaler elements and output 1:1 pixel ratio to the output devices.

The `cam-pipeline` program will create a named FIFO at `/tmp/cam-screencap.jpg`,
and operates the write end of the FIFO. When this FIFO is opened, the pipeline will
encode the current frame as a JPEG and write it into the FIFO. Thus the command
`cat /tmp/cam-screencap.jpg > somefile.jpg` can be used to take a live screenshot
from the pipeline.

The DBus interface to the video pipeline daemon is accessible at
`/com/krontech/chronos/video` and conforms to the interface given by
[com.krontech.chronos.video.xml](../../src/api/com.krontech.chronos.video.xml), which
implements the methods:

* [`status`](#status): Return the status of the video pipeline.
* [`addregion`](#addregion): Add a new video region to the playback mode.
* [`playback`](#playback): Control the frame position and playback rate.
* [`recordfile`](#recordfile): Encode and write vide to a file.
* `livestream`: Configure network video streams (TODO)

And emits the DBus signal:
 * [`eof`](#eof): Video recording is complete and the pipeline has exited record mode.

status
------
Returns the current status of the video pipeline, This method takes no
arguments, and the returned hash map will contain the following members.

| Output            | Type      | Description
|:----------------- |:--------- |:--------------
| `"apiVersion"`    | `string`  | `"1.0"` for all cameras implemeting this specification.
| `"playback"`      | `boolean` | `true` if the video pipeline is in playback mode.
| `"recording"`     | `boolean` | `true` if the video pipeline is in record mode.
| `"position"`      | `uint`    | The current frame number being display while in playback or record mode.
| `"totalFrames"`   | `uint`    | The total number of frames across all recorded segments.
| `"segment"`       | `uint`    | The segment to which the current frame belongs.
| `"framerate"`     | `int`     | The target playback rate when in playback mode, or estimated frame rate when in record mode.

addregion
---------
Informs the pipeline about a new recording segment in video memory, this method
will create a new segment and append it to the end of the list of known recording
segments. Any existing segments which would overlap the new one will be deleted.
This function takes a hash map with the following members, and returns no values.


| Input             | Type      | Description
|:----------------- |:--------- |:--------------
| `"base"`          | `uint`    | The starting address, in words, of the new recording region.
| `"size"`          | `uint`    | The size, in words, of the new recording region.
| `"offset"`        | `uint`    | The offset from `base` of the first frame in the new region.

playback
--------
Sets the framerate when in playback mode. The framerate can be set to positive numbers to play
the video foreward, or to negative values to play backwards. A value of zero will pause the
video. The caller can also specify a frame number from which to begin playback. This method
takes the following optional arguments.

| Input             | Type      | Description
|:----------------- |:--------- |:--------------
| `"framerate"`     | `int`     | The frame rate to use in playback mode.
| `"position"`      | `uint`    | The frame number to start playback from.

This method will return the same values as the [`status`](#status) method.

recordfile
----------
Select a range of frames to be encoded, and provide optional recording parameters. Upon calling
this method, the pipeline will enter record mode to write those frames to a file.

| Input             | Type      | Description
|:----------------- |:--------- |:--------------
| `"filename"`      | `string`  | The destination file to be written.
| `"start"`         | `uint`    | The starting frame number of the recording region.
| `"length"`        | `uint`    | The number of frames to be recorded.
| `"format"`        | `string`  | The encoding format to select.
| `"framerate"`     | `uint`    | The desired framerate of the encoded video file, in frames per second.
| `"maxBitrate"`    | `uint`    | The maximum encoded bitrate for compressed formats, in bits per second.
| `"bisPerPixel"`   | `float`   | The encoding quality for compressed formats.

The `"format"` argument can take one of the following values:
 * `"h264"`: H.264 compressed video saved in an MPEG-4 container.
 * `"raw"`: Raw video in the pixel format provided by the image sensor (see the `get_sensor_data` method for more details).
 * `"dng"`: Cinema-DNG uncompressed video (TODO).
 * `"png"`: Sequence of PNG images, for this format the `"filename"` argument should provide the destination directory.

When saving in the `"raw"` format, the `"bitsPerPixel"` parameter may optionally be used to pad
or truncate the pixel encoding format. The default pixel format will match the encoding specified
by the `get_sensor_data` API found in the camera control daemon. Padding and truncation will either
add or remove the least significant bits of the pixel encoding. For example, if a 12-bit sensor
returned the pixel value `0x800`, setting `"bitsPerPixel"` to 16 would encode the pixel as `0x8000`.

TODO: I would personally like to see the raw encoding specified using a FOURCC code and then expect
the pipeline to figure out the padding and truncation necessary. But that gives the users a lot of
rope to hang themselves with depending on the complexity of the translation. For example, BRY2 -> Y16
makes sense, but BYR2 -> NV12 would be a pain in the butt to implement.

The `recordfile` function will immediately return a hash map containing the following
members.

| Output            | Type      | Description
|:----------------- |:--------- |:--------------
| `"status"`        | `string`  | `"success"` if the pipeline has entered record mode, or a descriptive error string otherwise.
| `"filename"`      | `string`  | The full path of the file, or directory, being written to.
| `"estimatedSize"` | `uint`    | The estimated file size, in bytes.

eof
---
The `eof` DBus signal is emitted by the pipeline when video recording has finished, upon emitting the `eof`,
the pipeline will send itself a `SIGHUP` to reconfigure the pipeline and return to either playback or live
display mode. The `eof` signal will include a hash map containing the following members.

| Output            | Type      | Description
|:----------------- |:--------- |:--------------
| `"status"`        | `string`  | `"success"` if the video file was successfully written, or a descriptive error string otherwise.
| `"filename"`      | `string`  | The full path of the file, or directory, being written to.
| `"filesize"`      | `uint`    | The total number of bytes written.


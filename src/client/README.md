Chronos Command Line Recipes
============================
The `client` directory contains a set of tools for manipulating the Chronos 
camera from the command line. There are two executable programs `cam-json`
and `cam-listener` which translate between the D-Bus API and JSON.

The camera includes two D-Bus interfaces:
 * The `video` interface runs manages video playback from the FPGA and the
   GStreamer pipeline.
 * The `control` interface runs the capture and acquisition side of the FPGA,
   as well as shared state between UI applications. (TBD).

Please refer to the [Video Pipeline Documentation](../pipeline/README.md) for
the normative API documentation of the `video` interface.

`cam-json`
----------
The `cam-json` tool is used to invoke a D-Bus API method from the command
line. Parameters, when present, are passed into this tool via `stdin` in
JSON encoding.

```
usage : /opt/camera/cam-json [options] METHOD [PARAMS]

Make a DBus call to the Chronos camera daemon, and translate
the result into JSON. Parameters passed to the RPC call will
be parsed from the PARAMS file, if provided.

options:
	-r, --rpc     encode the results in JSON-RPC format
	-c, --cgi     encode the results in CGI/1.0 format
	-n, --control connect to the control DBus interface
	-v, --video   connect to the video DBus interface
	-h, --help    display this help and exit
```

A typical invocation, with no parameters might be as follows:

```
root@dm814x-evm:~# /opt/camera/cam-json -v status
{
   "apiVersion": "1.0",
   "position": 79,
   "recording": false,
   "playback": true,
   "segment": 0,
   "framerate": 60,
   "totalFrames": 1860
}
```

When parameters are required, we can pipe them into `cam-json` as
follows:

```
root@dm814x-evm:~# echo '{"position": 0, "framerate":120}' | /opt/camera/cam-json -v playback - 
{
   "apiVersion": "1.0",
   "position": 0,
   "recording": false,
   "playback": true,
   "segment": 0,
   "framerate": 60,
   "totalFrames": 1860
}
```

`cam-listener`
--------------
The `cam-listener` connects to the D-Bus API, and listens for asynchronous signals. When
a signal is received, it is converted into JSON format and printed to `stdout`.

```
usage : /opt/camera/cam-listener [options]

Listen for DBus signals from the Chronos camera daemons, and
translate them into JSON. The resulting event stream can be
encoded as JSON, or wrapped as HTML5 Server-Sent-Events.

options:
	-s, --sse     encode the signals as an HTML5 SSE stream
	-n, --control connect to the control DBus interface
	-v, --video   connect to the video DBus interface
	-h, --help    display this help and exit
```

Asynchronous changes to the video system can be reported as follows:

```
root@dm814x-evm:~# /opt/camera/cam-listener -v     
{
   "totalFrames": 1860,
   "apiVersion": "1.0",
   "position": 50,
   "recording": false,
   "playback": true,
   "segment": 0,
   "framerate": 60
}
```

`cam-recordfile.sh`
-------------------
The `cam-recordfile.sh` script is a wrapper for `cam-json` to generate the appropriate
API calls to start a file encoding process.

```
Usage: cam-recordfile.sh [options] FILE

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
```

The `cam-recordfile.sh` script will return immediately upon success, and the video
system will transition into the recording state and generate an `sof` event. When
recording is complete an `eof` event will be generated and the video system will
transition to the playback state.

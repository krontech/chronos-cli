Video Pipeline Interface
************************

The ``cam-pipeline`` program is responsible for managing the multimedia pipeline
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
``playback`` function.

In record mode, the FPGA replays video in the same manner as playback mode, but
video stream will instead be passed through an encoder element and will be written
to a file rather than the output devices. When the recording is complete, the
pipeline will send itself a SIGHUP to return to live display or playback mode.

The ``cam-pipeline`` program will respond to the following signals:

    * ``SIGHUP``: Reboot the video pipeline and update its configuration.
    * ``SIGINT``: Terminate the pipeline and shut down gracefully.
    * ``SIGUSR1``: Reload recording segment data from the FPGA.
    * ``SIGUSR2``: Disable the video scaler elements and output 1:1 pixel ratio to the output devices.

The ``cam-pipeline`` program will create a named FIFO at ``/tmp/cam-screencap.jpg``,
and operates the write end of the FIFO. When this FIFO is opened, the pipeline will
encode the current frame as a JPEG and write it into the FIFO. Thus the command
``cat /tmp/cam-screencap.jpg > somefile.jpg`` can be used to take a live screenshot
from the pipeline.

The DBus interface to the video pipeline daemon is accessible at
``/com/krontech/chronos/video`` and conforms to the interface given by
:download:`com.krontech.chronos.video.xml<../../src/api/com.krontech.chronos.video.xml>`

status
---------------
Returns the current status of the video pipeline, This function takes no
arguments, and the returned hash map will contain the following members.

=================== =========== ==============
Output              Type        description
------------------- ----------- --------------
``"apiVersion"``    ``string``  "1.0" for all cameras implemeting this specification.
``"playback"``      ``boolean`` ``true`` if the video pipeline is in playback mode.
``"recording"``     ``boolean`` ``true`` if the video pipeline is in record mode.
``"currentFrame"``  ``uint``    The current frame number being display while in playback or record mode.
``"totalFrames"``   ``uint``    The total number of frames across all recorded segments.
``"segment"``       ``uint``    The segment to which the current frame belongs.
``"framerate"``     ``int``     The target playback rate when in playback mode, or estimated frame rate when in record mode.
=================== =========== ==============

addregion
---------------
Informs the pipeline about a new recording segment in video memory, this function
will create a new segment and append it to the end of the list of known recording
segments. Any existing segments which would overlap the new one will be deleted.
This function takes a hash map with the following members, and returns no values.

=================== =========== ==============
Input               Type        description
------------------- ----------- --------------
``"base"``          ``uint``    The starting address, in words, of the new recording region.
``"size"``          ``uint``    The size, in words, of the new recording region.
``"offset"``        ``uint``    The offset from ``base`` of the first frame in the new region.
=================== =========== ==============

playback
---------------
Sets the framerate when in playback mode. The framerate can be set to positive numbers to play
the video foreward, or to negative values to play backwards. A value of zero will pause the
video. The caller can also specify a frame number from which to begin playback. This function
takes the following optional arguments.

=================== =========== ==============
Input               Type        description
------------------- ----------- --------------
``"framerate"``     ``int``     The frame rate to use in playback mode.
``"position"``      ``uint``    The frame number to start playback from.
=================== =========== ==============

This function will return the values as the ``status`` function.

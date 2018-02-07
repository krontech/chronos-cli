Camera Control Interface
************************

The ``cam-daemon`` program is responsible for interfacing with the FPGA and image
sensor, as well as managing the triggers and recording sequence.

The DBus interface to the camera control daemon is accessible at
``/com/krontech/chronos/control`` and conforms to the interface given by
:download:`com.krontech.chronos.control.xml<../../src/api/com.krontech.chronos.control.xml>`

get_camera_data
---------------
Returns a description of the camera and its hardware options. This function takes no
arguments, and the returned hash map will contain the following members.

=================== =========== ==============
Output              Type        description
------------------- ----------- --------------
``"model"``         ``string``  Descriptive string of the camera model name.
``"serial"``        ``string``  Camera serial number.
``"apiVersion"``    ``string``  "1.0" for all cameras implemeting this specification.
``"fpgaVersion"``   ``string``  Major and minor version numbers of the FPGA bitstream version.
``"memoryGB"``      ``uint``    Video capture memory capacity, in Gigabytes.
=================== =========== ==============

get_sensor_data
---------------
Returns a description of the image sensor and its capabilties. This function takes no
arguments, and the returned hash map will contain the following members.

=================== =========== ==============
Output              Type        description
------------------- ----------- --------------
``"name"``          ``string``  Descriptive string of the image sensor.
``"hMax"``          ``uint``    Maximum horizontal resolution in pixels.
``"vMax"``          ``uint``    Maximum vertical resolution in pixels.
``"hMin"``          ``uint``    Minimum horizontal resolution in pixels.
``"vMin"``          ``uint``    Minimum vertical resolution in pixels.
``"hIncrement"``    ``uint``    Horizontal resolution increment, the selected resolution must always be an integer multiple of this number.
``"vIncrement"``    ``uint``    Vertical resolution increment, the selected resolution must always be an integer multiple of this number.
``"pixelRate"``     ``uint``    Maximum pixel throughput in pixels per second.
``"pixelFormat"``   ``string``  FOURCC code describing the pixel format and bit depth, refer to https://dri.freedesktop.org/docs/drm/media/uapi/v4l/pixfmt.html for more information.
=================== =========== ==============

get_timing_limits
-----------------
Takes as input a desired image resolution, and returns the timing constraints at that
resolution. This function take the following parameters as input.

=================== =========== ==============
Input               Type        description
------------------- ----------- --------------
``"hRes"``          ``uint``    Desired horizontal resolution in pixels.
``"vRes"``          ``uint``    Desired vertical resolution in pixels.
=================== =========== ==============

The ``get_timing_limits`` function will then return a hash map with the following 
members.

======================= =========== ==============
Output                  Type        description
----------------------- ----------- --------------
``"tMinPeriod"``        ``uint``    Minimum allowable frame period in nanoseconds.
``"tMaxPeriod"``        ``uint``    Maximum allowable frame period in nanoseconds.
``"tMinExposure"``      ``uint``    Minimum allowable exposure time in nanoseconds.
``"tExposureDelay"``    ``uint``    Exposure overhead in nanoseconds.
``"tMaxShutterAngle"``  ``uint``    Maximum shutter angle in degrees.
``"fQuantization"``     ``uint``    Internal timer frequency in Hertz.
======================= =========== ==============

This information can be used to determine the timing constraints at a given resolution.
The frame period must be constrained such that:

    * ``(tMinPeriod <= tFramePeriod) && (tFramePeriod <= tMaxPeriod)``

The exposure timing limits are a function of the frame period, and the selected exposure must
satisfy the two following constraints:

    * ``tMinExposure <= tExposure``, and
    * ``tExposure <= (tFramePeriod * tMaxShutterAngle) / 360 - tExposureDelay``

All timing parameters given to the control daemon will be implicitly quanitized to the frequency
given by ``fQuantization``
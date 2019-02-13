import pychronos
from abc import ABC, abstractmethod

class frameGeometry:
    def __init__(self, hRes, vRes, hOffset=0, vOffset=0, vDarkRows=0, bitDepth=12):
        self.hRes = hRes
        self.vRes = vRes
        self.hOffset = hOffset
        self.vOffset = vOffset
        self.vDarkRows = vDarkRows
        self.bitDepth = bitDepth
    
    def __repr__(self):
        args = (self.hRes, self.vRes, self.hOffset, self.vOffset, self.vDarkRows, self.bitDepth)
        return "frameGeometry(hRes=%s, vRes=%s, hOffset=%s, vOffset=%s, vDarkRows=%s, bitDepth=%s)" % args
    
    def pixels(self):
        return self.hRes * (self.vRes + self.vDarkRows)
    
    def size(self):
        return self.pixels() * self.bitDepth // 8

# Live frame reader class
## FIXME: This will hang if the camera is currently recording, in which case
## lastAddr and writeAddr will point to somewhere in the recording region. We
## can work around it by getting the address from seq.writeAddr or lastAddr
##
## FIXME: Does this require locking or other mutual exclusion mechanisms to
## ensure multiple readout functions don't collide and do bad things?
##
## TODO: Would this be better done as a method of the pychronos.sequencer class,
## in which case the calling convention might be something like, with the pixel
## counting registers, the hRes and vRes could be made optional by inferring
## them from the FPGA.
##
## seq = pychronos.sequencer()
## for delay in seq.startLiveReadout():
##     time.sleep(delay)
## frame = seq.liveReadout()
class liveReadout(pychronos.sequencer):
    """Live frame readout class

    This helper class is typically used during calibration routines to
    extract frames without interrupting the live display or recording
    sequencer. Once created, a live readout operation may be initiated
    using the startLiveReadout() function, which returns a generator
    that will yield until frame readout is completed.

    Attributes
    ----------
    result: frame or None
        The result of the frame readout operation, or None if still
        in progress. 

    Parameters
    ----------
    hRes : int
        The horizontal resolution of image data to read out.
    vRes : int
        The vertical resolution of image data to read out.
    """
    def __init__(self, hRes, vRes):
        super().__init__()
        self.hRes = hRes
        self.vRes = vRes
        self.backup = [self.liveAddr[0], self.liveAddr[1], self.liveAddr[2]]
        self.page = 0
        self.result = None
    
    def startLiveReadout(self):
        """Begin readout of a frame from the live display buffer.

        This helper function is typically used during calibration routines to
        extract frames without interrupting the live display or recording
        sequencer. The frame is returned to the caller via a callback function,
        with the frame as its argument.

        Returns
        -------
        generator (float)
            Sleep time, in seconds, between steps of the readout proceedure.
        """
        # Set all three live buffers to the same address.
        self.liveAddr[0] = self.backup[self.page]
        self.liveAddr[1] = self.backup[self.page]
        self.liveAddr[2] = self.backup[self.page]
        self.result = None

        # Wait for the sequencer to begin writing to the updated live address.
        while (self.writeAddr != self.backup[self.page]):
            yield 0.001 # 1ms
        
        # Switch page and readout a frame.
        self.page ^= 1
        self.result = pychronos.readframe(self.backup[self.page], self.hRes, self.vRes)
        self.liveAddr[0] = self.backup[0]
        self.liveAddr[1] = self.backup[1]
        self.liveAddr[2] = self.backup[2]

# Abstract Sensor class
class sensor(ABC):
    def __init__(self):
        super().__init__()
    
    #--------------------------------------------
    # Sensor Configuration and Control API
    #--------------------------------------------
    @abstractmethod
    def reset(self, fSize=None):
        """Perform a reset of the image sensor and bring it into normal operation.
        
        This function may be called either to initialzie the image sensor, or to restart it
        if the sensor is already running. After a reset, the expected state of the image
        sensor is to be operating at full-frame resolution, the maximum framerate, nominal
        (0dB) gain and a 180-degree shutter angle.

        This function returns no value, but may throw an exception if the initialization
        proceedure failed.

        Parameters
        ----------
        fSize : frameGeometry (optional)
            The initial video geometry to set after initializing the image sensor, or
            use the default video geometry by default.
        """
        pass

    #--------------------------------------------
    # Frame Geometry Configuration Functions
    #--------------------------------------------
    @abstractmethod
    def getMaxGeometry(self):
        """Return the maximum frame geometry supported by the image sensor
        
        Returns
        -------
        frameGeometry
        """
        pass
    
    @abstractmethod
    def getCurrentGeometry(self):
        """Return the current frame size of the image sensor
        
        Returns
        -------
        frameGeometry
        """
        pass
    
    def isValidResolution(self, size):
        """Test if the provided geometry is supported by the iamge sensor"""
        # Default implementation only checks agianst the maximums. Sensors
        # should normally override this with some more accurate sanity checks.
        limits = self.getMaxGeometry()
        if ((size.hRes + size.hOffset) > limits.hRes):
            return False
        if ((size.vRes + size.vOffset) > limits.vRes):
            return False
        if (size.vDarkRows > limits.vDarkRows):
            return False
        if (size.bitDepth != limits.bitDepth):
            return False
        return True

    @abstractmethod
    def setResolution(self, size, fPeriod=None):
        """Configure the resolution and frame geometry of the image sensor"""
        pass

    #--------------------------------------------
    # Frame Timing Configuration Functions
    #--------------------------------------------
    @abstractmethod
    def getPeriodRange(self, size):
        """Return a tuple with the minimum and maximum frame periods at a given frame size
        
        Returns
        -------
        (float, float) : A tuple of (min, max) frame periods in seconds, or zero to
            indicate that the frame period is not limited.
        """
        pass

    @abstractmethod
    def getCurrentPeriod(self):
        """Return the current frame period of the image sensor
        
        Returns
        -------
        float : Current frame period in seconds
        """
        pass
    
    @abstractmethod
    def setFramePeriod(self, fPeriod):
        """Configure the frame period of the image sensor"""
        pass

    @abstractmethod
    def getExposureRange(self, size, period):
        """Return a tuple with the minimum and maximum exposure at a given frame size
        
        Returns
        -------
        (float, float) : A tuple of (min, max) exposure periods in seconds, or zero to
            indicate that te exposure period is not limited.
        """
        pass
    
    @abstractmethod
    def getCurrentExposure(self):
        """Return the current image sensor exposure time
        
        Returns
        -------
        float : The current exposure time in seconds.
        """
        pass
    
    @abstractmethod
    def setExposurePeriod(self, expPeriod):
        """Configure the exposure time for the image sensor"""
        pass
    
    #--------------------------------------------
    # Sensor Analog Calibration Functions
    #--------------------------------------------
    def getColorMatrix(self, cTempK=5500):
        """Return the color correction matrix for this image sensor.

        A color matrix is required to convert the image sensor's bayer filter data into
        sRGB color space. If the image sensor is characterized under multiple lighting
        conditions, the matrix which best matches the provided color temperature should
        be returned.
        
        Parameters
        ----------
        cTempK: int, optional
            The color temperature (degrees Kelvin) of the CIE D-series illuminant under
            which the color matrix will be used (default 5500K).

        Returns
        -------
        [[float]] : A 3x3 matrix of floats converting the camera color space to sRGB.
        """
        ## Return an identity matrix if not implemented.
        return [[1.0, 0, 0], [0, 1.0, 0], [0, 0, 1.0]]
    
    @abstractmethod
    def startAnalogCal(self):
        """Perform the automatic analog sensor calibration at the current settings.

        Any analog calibration that the sensor can perform without requiring any
        extern user setup, such as covering the lens cap or attaching calibration
        jigs, should be performed by this call.

        Yields
        ------
        float :
            The leep time, in seconds, between steps of the calibration proceedure.
        
        Examples
        --------
        This function returns a generator iterator with the sleep time between the
        steps of the analog calibration proceedure. The caller may use this for
        cooperative multithreading, or can complete the calibration sychronously
        as follows:

        for delay in sensor.startAnalogCal():
            time.sleep(delay)
        """
        pass
    
    def loadAnalogCal(self):
        """Load stored analog calibration data from a file, if supported"""
        raise NotImplementedError
    
    def saveAnalogCal(self):
        """Write analog calibration data from a file, if supported"""
        raise NotImplementedError
    
    @abstractmethod
    def setGain(self, gain):
        """Configure the analog gain of the image sensor"""
        pass

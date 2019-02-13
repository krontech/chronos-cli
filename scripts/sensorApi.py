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
    
    @abstractmethod
    def boot(self, size=None):
        """Perform a boot of the image sensor and bring it into normal operation"""
        pass

    @abstractmethod
    def getMaxGeometry(self):
        """Return the maximum frame geometry supported by the image sensor"""
        pass

    @abstractmethod
    def getPeriodRange(self, size):
        """Return a tuple with the minimum and maximum frame period at a given frame size"""
        pass

    @abstractmethod
    def getExposureRange(self, size, period):
        """Return a tuple with the minimum and maximum exposure at a given frame size"""
        pass

    @abstractmethod
    def setResolution(self, size, fPeriod=None):
        """Configure the resolution and frame geometry of the image sensor"""
        pass

    @abstractmethod
    def setExposurePeriod(self, expPeriod):
        """Configure the exposure time for the image sensor"""
        pass
    
    @abstractmethod
    def setFramePeriod(self, fPeriod):
        """Configure the frame period of the image sensor"""
        pass

    @abstractmethod
    def setGain(self, gain):
        """Configure the analog gain of the image sensor"""
        pass

    @abstractmethod
    def startAnalogCal(self):
        """Perform the automatic analog sensor calibration at the current settings.

        Any analog calibration that the sensor can perform without requiring any
        extern user setup, such as covering the lens cap or attaching calibration
        jigs, should be performed by this call.

        This function returns a generator iterator with the sleep time between the
        steps of the analog calibration proceedure. The caller may use this for
        cooperative multithreading, or can complete the calibration sychronously
        as follows:

        for delay in sensor.autoAnalogCal():
            time.sleep(delay)

        Returns
        -------
        generator (float)
            Sleep time, in seconds, between steps of the calibration proceedure.
        """
        pass
    
    def loadAnalogCal(self):
        """Load stored analog calibration data from a file, if supported"""
        raise NotImplementedError
    
    def saveAnalogCal(self):
        """Write analog calibration data from a file, if supported"""
        raise NotImplementedError
    
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

    ## FIXME: This will hang if the camera is currently recording, in which case
    ## lastAddr and writeAddr will point to somewhere in the recording region.
    ##
    ## FIXME: Does this require locking or other mutual exclusion mechanisms to
    ## ensure multiple readout functions don't collide and do bad things?
    def readFromLive(self, hRes, vRes, callback, numFrames=1):
        """Read frames from the live display buffer

        This helper function is typically used during calibration routines to
        extract frames without interrupting the live display or recording
        sequencer. The frame is returned to the caller via a callback function,
        with the frame as its argument.

        Parameters
        ----------
        hRes : int
            The horizontal resolution of image data to read out.
        vRes : int
            The vertical resolution of image data to read out.
        callback : Callable[[int], None]
            Callback function to be executed for each frame read.
        frameList : int, optional
            The number of frames to read out (default: 1)

        Returns
        -------
        generator (float)
            Sleep time, in seconds, between steps of the readout proceedure.
        """
        seq = pychronos.sequencer()
        backup = [seq.liveAddr[0], seq.liveAddr[1], seq.liveAddr[2]]
        page = 0

        for i in range(0, numFrames):
            # Set all three live buffers to the same address.
            seq.liveAddr[0] = backup[page]
            seq.liveAddr[1] = backup[page]
            seq.liveAddr[2] = backup[page]

            # Wait for the sequencer to being writing to the update live address.
            while (seq.writeAddr != backup[page]):
                yield 0.001 # 1ms

            # Read a frame then switch pages.
            page ^= 1
            frame = pychronos.readframe(backup[page], hRes, vRes)
            if (callback):
                callback(frame)

        ## Restore the live display buffers to normal operation.
        seq.liveAddr[0] = backup[0]
        seq.liveAddr[1] = backup[1]
        seq.liveAddr[2] = backup[2]
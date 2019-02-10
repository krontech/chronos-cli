# Require abstract class helpers
from abc import ABC, abstractmethod

class frameGeometry:
    def __init__(self, hRes, vRes, hOffset=0, vOffset=0, vDarkRows=0, bitDepth=12):
        self.hRes = hRes
        self.vRes = vRes
        self.hOffset = hOffset
        self.vOffset = vOffset
        self.vDarkRows = 0
        self.bitDepth = bitDepth
    
    def __repr__(self):
        args = (self.hRes, self.vRes, self.hOffset, self.vOffset, self.vDarkRows, self.bitDepth)
        return "frameGeometry(hRes=%s, vRes=%s, hOffset=%s, vOffset=%s, vDarkRows=%s, bitDepth=%s)" % args
    
    def pixels(self):
        return self.hRes * (self.vRes * self.vDarkRows)
    
    def size(self):
        return self.pixels() * self.bitDepth // 8

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
    def autoAnalogCal(self):
        """Perform automatic analog calibration at the current recording settings"""
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

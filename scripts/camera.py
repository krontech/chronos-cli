# Chronos High-Speed Camera class
import pychronos
import fcntl
import time
import os
import numpy

from sensorApi import liveReadout
from spd import spd

class camera:
    BYTES_PER_WORD = 32
    FRAME_ALIGN_WORDS = 64

    MAX_FRAME_WORDS = 0x10000
    CAL_REGION_START = 0
    CAL_REGION_FRAMES = 3
    LIVE_REGION_START = (CAL_REGION_START + MAX_FRAME_WORDS * CAL_REGION_FRAMES)
    LIVE_REGION_FRAMES = 3
    REC_REGION_START = (LIVE_REGION_START + MAX_FRAME_WORDS * LIVE_REGION_FRAMES)
    FPN_ADDRESS = CAL_REGION_START

    def __init__(self, sensor):
        # Store a reference to the image sensor.
        self.sensor = sensor

        # Setup the FPGA
        # HACK: This should be done outside of this class.
        os.system("cam-loader /var/camera/FPGA.bit")
        config = pychronos.config()
        config.sysReset = 1
        time.sleep(0.2)
        print("Loaded FPGA Version %s.%s" % (config.version, config.subver))

        # Setup memory
        self.ramSizeWords = self.setupMemory() // self.BYTES_PER_WORD

        # Setup live display
        sensorRegs = pychronos.sensor()
        sensorRegs.fifoStart = 0x100
        sensorRegs.fifoStop = 0x100

        sequencerRegs = pychronos.sequencer()
        sequencerRegs.liveAddr[0] = self.LIVE_REGION_START + self.MAX_FRAME_WORDS * 0
        sequencerRegs.liveAddr[1] = self.LIVE_REGION_START + self.MAX_FRAME_WORDS * 1
        sequencerRegs.liveAddr[2] = self.LIVE_REGION_START + self.MAX_FRAME_WORDS * 2
        self.geometry = self.sensor.getMaxGeometry()
        self.geometry.vDarkRows = 0
        self.setupDisplayTiming(self.geometry)
        self.setupRecordRegion(self.geometry, self.REC_REGION_START)
        sequencerRegs.frameSize = self.geometry.size() // self.BYTES_PER_WORD

        displayRegs = pychronos.display()
        displayRegs.control = displayRegs.COLOR_MODE

        # Load a default calibration
        colGainRegs = pychronos.fpgamap(pychronos.FPGA_COL_GAIN_BASE, 0x1000)
        colCurveRegs = pychronos.fpgamap(pychronos.FPGA_COL_CURVE_BASE, 0x1000)
        for x in range(0, self.geometry.hRes):
            colGainRegs.mem16[x] = (1 << 12)
            colCurveRegs.mem16[0] = 0

        # Reboot the sensor and enter live display mode
        self.sensor.boot()
        self.sensor.setResolution(self.geometry)

    def setupMemory(self):
        dimmSizes = [0, 0]
        configRegs = pychronos.config()

        # Probe the SODIMMs and initialize the RAM.
        try:
            dimmSpd = spd(0)
            dimmSizes[0] = dimmSpd.size
            print("Detected %s GB DIMM in slot 0" % (dimmSpd.size >> 30))
        except:
            pass
        
        try:
            dimmSpd = spd(1)
            dimmSizes[1] = dimmSpd.size
            print("Detected %s GB DIMM in slot 1" % (dimmSpd.size >> 30))
        except:
            pass

        # We require at least one DIMM to be present.
        if ((dimmSizes[0] + dimmSizes[1]) == 0):
            raise RuntimeError("Memory configuration failed, no DIMMs detected")

        if (dimmSizes[1] > dimmSizes[0]):
            # Swap DIMMs to put the largest one first.
            configRegs.mmuConfig = configRegs.MMU_INVERT_CS
        elif (dimmSizes[0] < (16 << 30)):
            # Stuff DIMMs together if less than 16GB
            configRegs.mmuConfig = configRegs.MMU_SWITCH_STUFFED

        # Return the total memory size
        return (dimmSizes[0] + dimmSizes[1])

    ## TODO: This function needs to get moved into the video pipeline
    ## daemon so that it can manage the video framerate switching as
    ## necessary.
    def setupDisplayTiming(self, fSize, framerate=60):
        displayRegs = pychronos.display()
        configRegs  = pychronos.config()

        hSync = 1
        hBackPorch = 64
        hFrontPorch = 4
        vSync = 1
        vBackPorch = 4
        vFrontPorch = 1
        pxClock = 100000000

        # FPGA revision 3.14 an higher use a 133MHz video clock.
        if ((configRegs.version > 3) or (configRegs.version == 3 and configRegs.subver >= 14)):
            pxClock = 133333333

        # Calculate the minum hPeriod to fit with 2048 max vertical resolution
        # and make sure the real hPeriod is equal or larger.
        hPeriod = hSync + hBackPorch + fSize.hRes + hFrontPorch
        minHPeriod = (pxClock // ((2048 + vBackPorch + vSync + vFrontPorch) * framerate)) + 1
        if (hPeriod < minHPeriod):
            hPeriod = minHPeriod

        # Calculate vPeriod to match the target framerate.
        vPeriod = pxClock // (hPeriod * framerate)
        if (vPeriod < (fSize.vRes + vBackPorch + vSync + vFrontPorch)):
            vPeriod = (fSize.vRes + vBackPorch + vSync + vFrontPorch)

        # Calculate the real FPS and generate some debug output.
        realFps = pxClock // (vPeriod * hPeriod)
        print("Setup display timing: %sx%s@%s (%sx%s max: %s)" % \
            ((hPeriod - hBackPorch - hSync - hFrontPorch), (vPeriod - vBackPorch - vSync - vFrontPorch), \
             realFps, fSize.hRes, fSize.vRes + fSize.vDarkRows, framerate))
        
        displayRegs.hRes = fSize.hRes
        displayRegs.hOutRes = fSize.hRes
        displayRegs.vRes = fSize.vRes + fSize.vDarkRows
        displayRegs.vOutRes = fSize.vRes + fSize.vDarkRows

        displayRegs.hPeriod = hPeriod - 1
        displayRegs.hSyncLen = hSync
        displayRegs.hBackPorch = hBackPorch
        displayRegs.vPeriod = vPeriod - 1
        displayRegs.vSyncLen = vSync
        displayRegs.vBackPorch = vBackPorch

    # Reconfigure the recording region for a given frame size.
    def setupRecordRegion(self, fSize, startAddr, frameCount=0):
        ## Figure out the frame size, in words.
        fSizeWords = (fSize.size() + self.BYTES_PER_WORD - 1) // self.BYTES_PER_WORD
        fSizeWords //= self.FRAME_ALIGN_WORDS
        fSizeWords *= self.FRAME_ALIGN_WORDS

        seq = pychronos.sequencer()
        seq.frameSize = fSizeWords
        seq.regionStart = startAddr
        if (frameCount != 0):
            # Setup the desired number of frames if specified.
            seq.regionStop = startAddr + (frameCount * fSizeWords)
        else:
            # Otherwise, setup the maximum available memory.
            seq.regionStop = (self.ramSizeWords // fSizeWords) * fSizeWords

    # Read the serial number - or make it an attribute?
    def getSerialNumber(self):
        I2C_SLAVE = 0x0703 # From linux/i2c-dev.h
        EEPROM_ADDR = 0x54 # From the C++ app

        # Open the I2C bus and set the EEPROM address.
        fd = os.open("/dev/i2c-1", os.O_RDWR)
        fcntl.ioctl(fd, I2C_SLAVE, EEPROM_ADDR)

        # Set readout offset and read the serial number.
        os.write(fd, bytearray([0, 0]))
        serial = os.read(fd, 12)
        os.close(fd)
        try:
            return serial.decode("utf-8")
        except:
            return ""

    def startBlackCal(self, numFrames=16, useLiveBuffer=True):
        # get the resolution from the display properties
        display = pychronos.display()
        xres = display.hRes
        yres = display.vRes

        print("Starting")

        # Readout and average the frames from the live buffer.
        # TODO: Setup the sequencer and do the same thing with a recording.
        fAverage = numpy.zeros((yres, xres))
        reader = liveReadout(xres, yres)
        for i in range(0, numFrames):
            yield from reader.startLiveReadout()
            fAverage += numpy.asarray(reader.result)
        fAverage /= numFrames

        # Readout the column gain and linearity calibration.
        colGainRegs = pychronos.fpgamap(pychronos.FPGA_COL_GAIN_BASE, display.hRes * 2)
        colCurveRegs = pychronos.fpgamap(pychronos.FPGA_COL_CURVE_BASE, display.hRes * 2)
        colOffsetRegs = pychronos.fpgamap(pychronos.FPGA_COL_OFFSET_BASE, display.hRes * 2)
        gain = numpy.asarray(colGainRegs.mem16, dtype=numpy.uint16) / (1 << 12)
        curve = numpy.asarray(colCurveRegs.mem16, dtype=numpy.int16) / (1 << 21)
        offsets = numpy.asarray(colOffsetRegs.mem16, dtype=numpy.int16)
        yield 0

        # For each column, the average gives the DC component of the FPN, which
        # gets applied to the column calibration as the constant term. The column
        # calibration function is given by:
        #
        # f(x) = curve * x^2 + gain * x + offset
        #
        # For the FPN to be black, we expected f(fpn) == 0, and therefore:
        #
        # offset = -(curve * fpn^2 + gain * fpn)
        colAverage = numpy.average(fAverage, 0)
        colOffset = curve * (colAverage * colAverage) + gain * colAverage
        # TODO: Would be nice to have a write helper.
        for x in range(0, xres):
            colOffsetRegs.mem16[x] = int(-colOffset[x]) & 0xffff
        yield 0

        # For each pixel, the AC component of the FPN can be found by subtracting
        # the column average, which we will load into the per-pixel FPN region as
        # a signed quantity.
        #
        # TODO: For even better calibration, this should actually take the slope
        # into consideration around the FPN, in which case we would also divide
        # by the derivative of f(x) for the column.
        fpn = numpy.int16(fAverage - colAverage)
        pychronos.writeframe(display.fpnAddr, fpn)

        print("---------------------------------------------")
        print("fpn details: min = %d, max = %d" % (numpy.min(fAverage), numpy.max(fAverage)))
        print("fpn standard deviation: %d" % (numpy.std(fAverage)))
        print("fpn standard deviation horiz: %s" % (numpy.std(fAverage, axis=1)))
        print("fpn standard deviation vert:  %s" % (numpy.std(fAverage, axis=0)))
        print("---------------------------------------------")

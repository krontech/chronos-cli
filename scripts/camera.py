# Chronos High-Speed Camera class
import pychronos
import fcntl
import time
import os
import numpy

from sequencer import sequencer, seqcommand
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
        self.sensor = sensor
    
    def reset(self, bitstream=None):
        """Reset the camera and initialize the FPGA and image sensor.

        Parameters
        ----------
        bitstream : str, optional
            File path to the FPGA bitstream to load, or None to perform
            only a soft-reset of the FPGA.
        """

        # Setup the FPGA if a bitstream was provided.
        if (bitstream):
            os.system("cam-loader %s" % (bitstream))
            config = pychronos.config()
            config.sysReset = 1
            time.sleep(0.2)
            print("Loaded FPGA Version %s.%s" % (config.version, config.subver))
        else:
            config = pychronos.config()
            config.sysReset = 1
            time.sleep(0.2)
            print("Detected FPGA Version %s.%s" % (config.version, config.subver))

        # Setup memory
        self.ramSize = self.setupMemory()

        # Setup live display
        sensorRegs = pychronos.sensor()
        sensorRegs.fifoStart = 0x100
        sensorRegs.fifoStop = 0x100

        sequencerRegs = sequencer()
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
        i = 0
        for row in self.sensor.getColorMatrix():
            for val in row:
                displayRegs.colorMatrix[i] = val
                i += 1

        # Load a default calibration
        colGainRegs = pychronos.fpgamap(pychronos.FPGA_COL_GAIN_BASE, 0x1000)
        colCurveRegs = pychronos.fpgamap(pychronos.FPGA_COL_CURVE_BASE, 0x1000)
        for x in range(0, self.geometry.hRes):
            colGainRegs.mem16[x] = (1 << 12)
            colCurveRegs.mem16[0] = 0

        # Reboot the sensor and enter live display mode
        self.sensor.reset()
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

        seq = sequencer()
        seq.frameSize = fSizeWords
        seq.regionStart = startAddr
        if (frameCount != 0):
            # Setup the desired number of frames if specified.
            seq.regionStop = startAddr + (frameCount * fSizeWords)
        else:
            # Otherwise, setup the maximum available memory.
            ramSizeWords = self.ramSize // self.BYTES_PER_WORD
            seq.regionStop = (ramSizeWords // fSizeWords) * fSizeWords

    def getMemorySize(self):
        return self.ramSize
    
    # Return the length of memory (in frames) minus calibration overhead.
    def getRecordingMaxFrames(self, fSize):
        ramSizeWords = self.ramSize // self.BYTES_PER_WORD - self.REC_REGION_START
        fSizeWords = (fSize.size() + self.BYTES_PER_WORD - 1) // self.BYTES_PER_WORD
        fSizeWords //= self.FRAME_ALIGN_WORDS
        fSizeWords *= self.FRAME_ALIGN_WORDS
        return ramSizeWords // fSizeWords

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
        """Begin the black calibration proceedure at the current settings.

        Black calibration takes a sequence of images with the lens cap or shutter
        closed and averages them to find the black level of the image sensor. This
        value can then be subtracted during playback to correct for image offset
        defects.

        Parameters
        ----------
        numFrames : int, optional
            The number of frames to use for black calibration (default 16 frames)
        useLiveBuffer : bool, optional
            Whether to use the live display for black calibration (default True)

        Yields
        ------
        float :
            The sleep time, in seconds, between steps of the calibration proceedure.
        
        Examples
        --------
        This function returns a generator iterator with the sleep time between the
        steps of the black calibration proceedure. The caller may use this for
        cooperative multithreading, or can complete the calibration sychronously
        as follows:

        for delay in camera.startBlackCal():
            time.sleep(delay)
        """
        # get the resolution from the display properties
        # TODO: We actually want to get it from the sequencer.
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
        # by the derivative of f'(fpn) for the column.
        fpn = numpy.int16(fAverage - colAverage)
        pychronos.writeframe(display.fpnAddr, fpn)

        print("---------------------------------------------")
        print("fpn details: min = %d, max = %d" % (numpy.min(fAverage), numpy.max(fAverage)))
        print("fpn standard deviation: %d" % (numpy.std(fAverage)))
        print("fpn standard deviation horiz: %s" % (numpy.std(fAverage, axis=1)))
        print("fpn standard deviation vert:  %s" % (numpy.std(fAverage, axis=0)))
        print("---------------------------------------------")

    def startZeroTimeBlackCal(self):
        """Begin the black calibration proceedure using a zero-time exposure.

        Black calibration is best performed with the lens cap or shutter closed,
        but in the absence of user intervention, an acceptable calibration can be
        achieved by taking a zero-time exposure instead.

        Parameters
        ----------
        numFrames : int, optional
            The number of frames to use for black calibration (default 16 frames)
        useLiveBuffer : bool, optional
            Whether to use the live display for black calibration (default True)

        Yields
        ------
        float :
            The sleep time, in seconds, between steps of the calibration proceedure.
        
        Examples
        --------
        This function returns a generator iterator with the sleep time between the
        steps of the black calibration proceedure. The caller may use this for
        cooperative multithreading, or can complete the calibration sychronously
        as follows:

        for delay in camera.startBlackCal():
            time.sleep(delay)
        """
        # Grab the current frame size and exposure.
        fSize = self.sensor.getCurrentGeometry()
        expPrev = self.sensor.getCurrentExposure()
        expMin, expMax = self.sensor.getExposureRange(fSize)

        # Reconfigure for the minum exposure supported by the sensor.
        self.sensor.setExposurePeriod(expMin)

        # Do a fast black cal from the live display buffer.
        # TODO: We might get better quality out of the zero-time cal by
        # testing a bunch of exposure durations and finding the actual
        # zero-time intercept.
        yield (3 / 60) # ensure the exposure time has taken effect.
        yield from self.startBlackCal(numFrames=2, useLiveBuffer=True)

        # Restore the previous exposure settings.
        self.sensor.setExposurePeriod(expPrev)

    def startRecording(self, program):
        """Program the recording sequencer and start recording.

        Parameters
        ----------
        program : `list` of `seqprogram`
            List of recording sequencer commands to execute for this recording.

        Yields
        ------
        float :
            The sleep time, in seconds, between steps of the recording.
        
        Examples
        --------
        This function returns a generator iterator with the sleep time between the
        steps of the recording proceedure. The caller may use this for cooperative
        multithreading, or can complete the calibration sychronously as follows:
        
        for delay in camera.startRecording():
            time.sleep(delay)
        """
        seq = sequencer()
        # Setup the nextStates into a loop, just in case the caller forgot and then
        # load the program into the recording sequencer.
        for i in range(0, len(program)):
            program[i].nextState = (i + 1) % len(program)
            seq.program[i] = program[i]
        
        # Begin recording.
        seq.control |= seq.START_REC
        seq.control &= ~seq.START_REC
        
    def stopRecording(self):
        seq = sequencer()
        seq.control |= seq.STOP_REC
        seq.control &= ~seq.STOP_REC

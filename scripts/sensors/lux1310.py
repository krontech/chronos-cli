#Luxima LUX1310 Image Sensor Class
import pychronos
import time
import math
import copy
import numpy
import logging

from regmaps import sequencer, sensorTiming
from sensors import api, frameGeometry
from . import lux1310wt

class lux1310(api):
    # Image sensor geometry constraints
    MAX_HRES = 1280
    MAX_VRES = 1024
    MIN_HRES = 192
    MIN_VRES = 96
    HRES_INCREMENT = 16
    VRES_INCREMENT = 2
    MAX_VDARK = 8
    BITS_PER_PIXEL = 12

    # Expected constants
    LUX1310_CHIP_ID = 0xDA
    LUX1310_SOF_DELAY = 0x0f
    LUX1310_LV_DELAY = 0x07
    LUX1310_MIN_HBLANK = 2
    LUX1310_SENSOR_HZ = 90000000
    LUX1310_TIMING_HZ = 100000000
    ADC_CHANNELS = 16
    ADC_FOOTROOM = 32
    ADC_OFFSET_MIN = -1023
    ADC_OFFSET_MAX = 1023

    # These are actually more of an FPGA thing...
    COL_GAIN_FRAC_BITS = 12
    COL_CURVE_FRAC_BITS = 21

    @property
    def name(self):
        return "LUX1310"
    
    @property
    def cfaPattern(self):
        if (self.sci.color):
            return ['R', 'G', 'G', 'B']
        else:
            return None

    def __init__(self):
        ## Hardware Resources
        self.sci = pychronos.lux1310()
        self.fpga = pychronos.sensor()
        self.wavetables = lux1310wt.wavetables
        self.timing = sensorTiming()

        ## ADC Calibration state
        self.adcOffsets = [0] * self.HRES_INCREMENT

        super().__init__()

    #--------------------------------------------
    # Sensor Configuration and Control API
    #--------------------------------------------
    def reset(self, fSize=None):
        self.timing.enabled = True
        self.timing.programStandard(100 * 4000 * 0.9, 100 * 3900 * 0.9)
        # Disable integration while setup is in progress.
        self.timing.stopTiming(waitUntilStopped=True)
        
        # Initialize the DAC voltage levels.
        self.sci.writeDAC(self.sci.DAC_VABL, 0.3)
        self.sci.writeDAC(self.sci.DAC_VRSTB, 2.7)
        self.sci.writeDAC(self.sci.DAC_VRST, 3.3)
        self.sci.writeDAC(self.sci.DAC_VRSTL, 0.7)
        self.sci.writeDAC(self.sci.DAC_VRSTH, 3.6)
        self.sci.writeDAC(self.sci.DAC_VDR1, 2.5)
        self.sci.writeDAC(self.sci.DAC_VDR2, 2.0)
        self.sci.writeDAC(self.sci.DAC_VDR3, 1.5)
        time.sleep(0.01) # Settling time

        # Force a reset of the image sensor.
        self.fpga.control |= self.fpga.RESET
        self.fpga.control &= ~self.fpga.RESET
        time.sleep(0.001)

        # Reset the SCI interface.
        self.sci.regSresetB = 0
        rev = self.sci.regChipId
        if (rev != self.LUX1310_CHIP_ID):
            print("LUX1310 regChipId returned an invalid ID (%s)" % (hex(rev)))
            return False
        else:
            print("Initializing LUX1310 silicon revision %s" % (self.sci.revChip))
        
        # Setup ADC training.
        self.sci.regCustPat = 0xFC0     # Set custom pattern for ADC training.
        self.sci.regTstPat = 2          # Enable test pattern
        self.sci.regPclkVblank = 0xFC0  # Set PCLK channel output during vertical blank
        self.sci.reg[0x5A] = 0xE1       # Configure for inverted DCLK output
        self.__autoPhaseCal()

        # Return to normal data mode
        self.sci.regPclkVblank = 0xf00          # PCLK channel output during vertical blanking
        self.sci.regPclkOpticalBlack = 0xfc0    # PCLK channel output during dark pixel readout
        self.sci.regTstPat = False              # Disable test pattern

        # Setup for 80-clock wavetable
        self.sci.regRdoutDly = 80               # Non-overlapping readout delay
        self.sci.reg[0x7A] = 80                 # Undocumented???

                # Set internal control registers to fine tune the performance of the sensor
        self.sci.regLvDelay = self.LUX1310_LV_DELAY     # Line valid delay to match internal ADC latency
        self.sci.regHblank = self.LUX1310_MIN_HBLANK    # Set horizontal blanking period

        # Undocumented internal registers from Luxima
        self.sci.reg[0x2D] = 0xE08E     # State for idle controls
        self.sci.reg[0x2E] = 0xFC1F     # State for idle controls
        self.sci.reg[0x2F] = 0x0003     # State for idle controls
        self.sci.reg[0x5C] = 0x2202     # ADC clock controls
        self.sci.reg[0x62] = 0x5A76     # ADC range to match pixel saturation level
        self.sci.reg[0x74] = 0x041F     # Internal clock timing
        self.sci.reg[0x66] = 0x0845     # Internal current control
        if (self.sci.revChip == 2):
            self.sci.reg[0x5B] = 0x307F # Internal control register
            self.sci.reg[0x7B] = 0x3007 # Internal control register
        elif (self.sci.revChip == 1):
            self.sci.reg[0x5B] = 0x301F # Internal control register
            self.sci.reg[0x7B] = 0x3001 # Internal control register
        else:
            # Unknown version - use silicon rev1 configuration
            print("Found LUX1310 sensor, unknown silicon revision: %s" % (self.sci.revChip))
            self.sci.reg[0x5B] = 0x301F # Internal control register
            self.sci.reg[0x7B] = 0x3001 # Internal control register

        for x in range(0, self.ADC_CHANNELS):
            self.sci.regAdcOs[x] = 0
        self.sci.regAdcCalEn = True

        # Configure for nominal gain.
        self.sci.regGainSelSamp = 0x007f
        self.sci.regGainSelFb = 0x007f
        self.sci.regGainBit = 0x03

        # Load the default (longest) wavetable and enable the timing engine.
        self.sci.wavetable(self.wavetables[0].wavetab)
        self.sci.regTimingEn = True
        time.sleep(0.01)

        # Start the FPGA timing engine.
        self.timing.continueTiming()
        self.timing.programStandard(100 * 4000 * 0.9, 100 * 3900 * 0.9)
        return True

    ## TODO: I think this whole function is unnecessary on the LUX1310 FPGA
    ## builds, even in the C++ camApp it doesn't run to completion, and the
    ## return codes are ignored.
    def __autoPhaseCal(self):
        """Private function - calibrate the FPGA data acquisition channels"""
        self.fpga.clkPhase = 0
        self.fpga.clkPhase = 1
        self.fpga.clkPhase = 0
        print("Phase calibration dataCorrect=%s" % (self.fpga.dataCorrect))
    
    #--------------------------------------------
    # Frame Geometry Configuration Functions
    #--------------------------------------------
    def getMaxGeometry(self):
        return frameGeometry(
            hRes=self.MAX_HRES, vRes=self.MAX_VRES,
            hOffset=0, vOffset=0,
            vDarkRows=self.MAX_VDARK,
            bitDepth=self.BITS_PER_PIXEL)
    
    def getCurrentGeometry(self):
        fSize = self.getMaxGeometry()
        fSize.hRes = self.sci.regXend - 0x20 + 1
        fSize.hOffset = self.sci.regXstart - 0x20
        fSize.hRes -= fSize.hOffset
        fSize.vOffset = self.sci.regYstart
        fSize.vRes = self.sci.regYend - fSize.vOffset + 1
        fSize.vDarkRows = self.sci.regNbDrkRows
        return fSize

    def isValidResolution(self, size):
            # Enforce resolution limits.
        if ((size.hRes < self.MIN_HRES) or (size.hRes + size.hOffset) > self.MAX_HRES):
            return False
        if ((size.vRes < self.MIN_VRES) or (size.vRes + size.vOffset) > self.MAX_VRES):
            return False
        if (size.vDarkRows > self.MAX_VDARK):
            return False
        if (size.bitDepth != self.BITS_PER_PIXEL):
            return False
        
        # Enforce minimum pixel increments.
        if ((size.hRes % self.HRES_INCREMENT) != 0):
            return False
        if ((size.vRes % self.VRES_INCREMENT) != 0):
            return False
        if ((size.vDarkRows % self.VRES_INCREMENT) != 0):
            return False
        
        # Otherwise, the resultion and offset are valid.
        return True
    
    def updateWavetable(self, size, frameClocks, gaincal=False):
        # Select the longest wavetable that gives a frame period longer than
        # the target frame period. Note that the wavetables are sorted by length
        # in descending order.
        wavetab = None
        for x in self.wavetables:
            wavetab = x
            if (frameClocks >= self.getMinFrameClocks(size, x.clocks)):
                break
        
        # If a suitable wavetabl exists, then load it.
        if (wavetab):
            self.sci.regTimingEn = False
            self.sci.regRdoutDly = wavetab.clocks
            self.sci.reg[0x7A] = wavetab.clocks
            if (gaincal):
                self.sci.wavetable(wavetab.gaintab)
            else:
                self.sci.wavetable(wavetab.wavetab)
            self.sci.regTimingEn = True
            self.fpga.startDelay = wavetab.abnDelay
            self.fpga.linePeriod = max((size.hRes // self.HRES_INCREMENT) + 2, wavetab.clocks + 3) - 1

            # set the pulsed pattern timing
            self.timing.setPulsedPattern(wavetab.clocks)
        # Otherwise, the frame period was probably too short for this resolution.
        else:
            raise ValueError("Frame period too short, no suitable wavetable found")

    def updateReadoutWindow(self, size):
        # Configure the image sensor resolution
        hStartBlocks = size.hOffset // self.HRES_INCREMENT
        hWidthBlocks = size.hRes // self.HRES_INCREMENT
        self.sci.regXstart = 0x20 + hStartBlocks * self.HRES_INCREMENT
        self.sci.regXend = 0x20 + (hStartBlocks + hWidthBlocks) * self.HRES_INCREMENT - 1
        self.sci.regYstart = size.vOffset
        self.sci.regYend = size.vOffset + size.vRes - 1
        self.sci.regDrkRowsStAddr = self.MAX_VRES + self.MAX_VDARK - size.vDarkRows + 4
        self.sci.regNbDrkRows = size.vDarkRows

    def setResolution(self, size, fPeriod=None):
        if (not self.isValidResolution(size)):
            raise ValueError("Invalid frame resolution")
        
        # Select the minimum frame period if not specified.
        minPeriod, maxPeriod = self.getPeriodRange(size)
        if (not fPeriod):
            fClocks = self.getMinFrameClocks(size)
        elif ((fPeriod * self.LUX1310_TIMING_HZ) >= minPeriod):
            fClocks = fPeriod * self.LUX1310_TIMING_HZ
        else:
            raise ValueError("Frame period too short")

        # Disable the FPGA timing engine and wait for the current readout to end.
        self.timing.stopTiming(waitUntilStopped=True)
        time.sleep(0.01) # Extra delay to allow frame readout to finish. 

        # Switch to the desired resolution pick the best matching wavetable.
        self.updateReadoutWindow(size)
        self.updateWavetable(size, frameClocks=fClocks)
        time.sleep(0.01)

        # Switch to the minimum frame period and 180-degree shutter after changing resolution.
        self.timing.programStandard(fClocks, fClocks * 0.95)
    
    #--------------------------------------------
    # Frame Timing Configuration Functions
    #--------------------------------------------
    def getMinFrameClocks(self, size, wtSize=0):
        # Select the longest wavetable that fits within the line readout time,
        # or fall back to the shortest wavetable for extremely small resolutions.
        if (wtSize == 0):
            ideal = (size.hRes // self.HRES_INCREMENT) + self.LUX1310_MIN_HBLANK - 3
            for x in self.wavetables:
                wtSize = x.clocks
                if (wtSize <= ideal):
                    break

        # Compute the minimum number of 90MHz LUX1310 sensor clocks per frame.
        # Refer to section 7.1 of the LUX1310 datasheet version v3.0
        tRead = size.hRes // self.HRES_INCREMENT
        tTx = 25        # hard-coded to 25 clocks in the FPGA, should be at least 350ns
        tRow = max(tRead + self.LUX1310_MIN_HBLANK, wtSize + 3)
        tFovf = self.LUX1310_SOF_DELAY + wtSize + self.LUX1310_LV_DELAY + 10
        tFovb = 41      # Duration between PRSTN falling and TXN falling (I think)
        tFrame = tRow * (size.vRes + size.vDarkRows) + tTx + tFovf + tFovb - self.LUX1310_MIN_HBLANK

        # Convert from LUX1310 sensor clocks to FPGA timing clocks.
        return (tFrame * self.LUX1310_TIMING_HZ) // self.LUX1310_SENSOR_HZ
    
    def getPeriodRange(self, fSize):
        # TODO: Need to validate the frame size.
        # TODO: Probably need to enforce some maximum frame period.
        clocks = self.getMinFrameClocks(fSize)
        return (clocks / self.LUX1310_TIMING_HZ, 0)
    
    def getCurrentPeriod(self):
        return self.timing.frameTime / self.LUX1310_TIMING_HZ

    def setFramePeriod(self, fPeriod):
        # TODO: Sanity-check the frame period.
        logging.debug('frame time: %f', math.ceil(fPeriod * self.LUX1310_TIMING_HZ))
        self.timing.frameTime = math.ceil(fPeriod * self.LUX1310_TIMING_HZ)
    
    def getExposureRange(self, fSize, fPeriod):
        # Defaulting to 1us minimum exposure and infinite maximum exposure.
        # TODO: Need a better handle on the exposure overhead.
        return (1.0 / 1000000, fPeriod - (500 / self.LUX1310_TIMING_HZ))

    def getCurrentExposure(self):
        return self.timing.integrationTime / self.LUX1310_TIMING_HZ
    
    def setExposurePeriod(self, expPeriod):
        # TODO: Sanity-check the exposure time.
        self.timing.integrationTime = math.ceil(expPeriod * self.LUX1310_TIMING_HZ)

    #--------------------------------------------
    # Sensor Analog Calibration Functions
    #--------------------------------------------
    def getColorMatrix(self, cTempK=5500):
        if not self.sci.color:
            # Identity matrix for monochrome cameras.
            return [[1.0, 0.0, 0.0],
                    [0.0, 1.0, 0.0],
                    [0.0, 0.0, 1.0]]
        else:
            # CIECAM16/D55
            return [[ 1.9147, -0.5768, -0.2342], 
                                [-0.3056,  1.3895, -0.0969],
                                [ 0.1272, -0.9531,  1.6492]]

    def setGain(self, gain):
        gainConfig = {  # VRSTB, VRST,  VRSTH,  Sampling Cap, Feedback Cap, Serial Gain
            1:          ( 2.7,   3.3,   3.6,    0x007f,       0x007f,       0x3),
            2:          ( 2.7,   3.3,   3.6,    0x0fff,       0x007f,       0x3),
            4:          ( 2.7,   3.3,   3.6,    0x0fff,       0x007f,       0x0),
            8:          ( 1.7,   2.3,   2.6,    0x0fff,       0x0007,       0x0),
            16:         ( 1.7,   2.3,   2.6,    0x0fff,       0x0001,       0x0),
        }
        if (not int(gain) in gainConfig):
            raise ValueError("Unsupported image gain setting")
        
        vrstb, vrst, vrsth, samp, feedback, sgain = gainConfig[int(gain)]
        self.sci.writeDAC(self.sci.DAC_VRSTB, vrstb)
        self.sci.writeDAC(self.sci.DAC_VRST, vrst)
        self.sci.writeDAC(self.sci.DAC_VRSTH, vrsth)
        self.sci.regGainSelSamp = samp
        self.sci.regGainSelFb = feedback
        self.sci.regGainBit = sgain
    
    def autoAdcOffsetIteration(self, fSize, numFrames=4):
        # Read out the calibration frames.
        fAverage = numpy.zeros((fSize.vDarkRows * fSize.hRes // self.ADC_CHANNELS, self.ADC_CHANNELS), dtype=numpy.uint32)
        seq = sequencer()
        for x in range(0, numFrames):
            yield from seq.startLiveReadout(fSize.hRes, fSize.vDarkRows)
            fAverage += numpy.reshape(seq.liveResult, (-1, self.ADC_CHANNELS))
        
        # Train the ADC offsets for a target of Average = Footroom + StandardDeviation
        fAverage /= numFrames
        adcAverage = numpy.average(fAverage, 0)
        adcStdDev = numpy.std(fAverage, 0)
        for col in range(0, self.ADC_CHANNELS):
            self.adcOffsets[col] -= (adcAverage[col] - adcStdDev[col] - self.ADC_FOOTROOM) / 2
            if (self.adcOffsets[col] < self.ADC_OFFSET_MIN):
                self.adcOffsets[col] = self.ADC_OFFSET_MIN
            elif (self.adcOffsets[col] > self.ADC_OFFSET_MAX):
                self.adcOffsets[col] = self.ADC_OFFSET_MAX

            # Update the image sensor
            self.sci.regAdcOs[col] = self.adcOffsets[col]

    def autoAdcOffsetCal(self, fSize, iterations=16):
        tRefresh = (self.timing.frameTime * 3) / self.LUX1310_TIMING_HZ
        tRefresh += (1/60)

        # Clear out the ADC offsets
        for i in range(0, self.ADC_CHANNELS):
            self.adcOffsets[i] = 0
            self.sci.regAdcOs[i] = 0
        
        # Enable ADC calibration and iterate on the offsets.
        self.sci.regAdcCalEn = True
        for i in range(0, iterations):
            yield tRefresh
            yield from self.autoAdcOffsetIteration(fSize)

    def autoAdcGainCal(self, fSize):
        # Setup some math constants
        numRows = 64
        tRefresh = (self.timing.frameTime * 10) / self.LUX1310_TIMING_HZ
        pixFullScale = (1 << fSize.bitDepth)

        seq = sequencer()
        colGainRegs = pychronos.fpgamap(pychronos.FPGA_COL_GAIN_BASE, 0x1000)
        colCurveRegs = pychronos.fpgamap(pychronos.FPGA_COL_CURVE_BASE, 0x1000)

        # Disable the FPGA timing engine and load the gain calibration wavetable. 
        self.timing.stopTiming(waitUntilStopped=True)
        time.sleep(0.1) # Extra delay to allow frame readout to finish. 
        self.updateWavetable(fSize, frameClocks=self.timing.frameTime, gaincal=True)
        self.timing.continueTiming()

        # Search for a dummy voltage high reference point.
        vhigh = 31
        while (vhigh > 0):
            self.sci.regSelVdumrst = vhigh
            yield tRefresh
            
            # Read a frame and compute the column averages and minimum.
            yield from seq.startLiveReadout(fSize.hRes, numRows)
            frame = numpy.reshape(seq.liveResult, (-1, self.ADC_CHANNELS))
            highColumns = numpy.average(frame, 0)

            # High voltage should be less than 7/8ths of full scale.
            if (numpy.amax(highColumns) <= (pixFullScale - (pixFullScale / 8))):
                break
            else:
                vhigh -= 1
        
        # Search for a dummy voltage low reference point.
        vlow = 0
        while (vlow < vhigh):
            self.sci.regSelVdumrst = vlow
            yield tRefresh

            # Read a frame and compute the column averages and minimum.
            yield from seq.startLiveReadout(fSize.hRes, numRows)
            frame = numpy.reshape(seq.liveResult, (-1, self.ADC_CHANNELS))
            lowColumns = numpy.average(frame, 0)

            # Find the minum voltage that does not clip.
            if (numpy.amin(lowColumns) >= self.ADC_FOOTROOM):
                break
            else:
                vlow += 1

        # Sample the midpoint, which should be somewhere around quarter scale.
        vmid = (vhigh + 3*vlow) // 4
        self.sci.regSelVdumrst = vmid
        yield tRefresh

        # Read a frame out of the live display.
        yield from seq.startLiveReadout(fSize.hRes, numRows)
        frame = numpy.reshape(seq.liveResult, (-1, self.ADC_CHANNELS))
        midColumns = numpy.average(frame, 0)

        logging.debug("ADC Gain calibration voltages=[%s, %s, %s]" % (vlow, vmid, vhigh))
        logging.debug("ADC Gain calibration averages=[%s, %s, %s]" %
                (numpy.average(lowColumns), numpy.average(midColumns), numpy.average(highColumns)))

        # Determine which column has the strongest response and sanity-check the gain
        # measurements. If things are out of range, then give up on gain calibration
        # and apply a gain of 1.0 instead.
        maxColumn = 0
        for col in range(0, self.ADC_CHANNELS):
            minrange = (pixFullScale // 16)
            diff = highColumns[col] - lowColumns[col]
            if ((highColumns[col] <= (midColumns[col] + minrange)) or (midColumns[col] <= (lowColumns[col] + minrange))):
                logging.warning("ADC Auto calibration range error")
                for x in range(0, self.MAX_HRES):
                    colGainRegs.mem16[x] = (1 << self.COL_GAIN_FRAC_BITS)
                    colCurveRegs.mem16[x] = 0
                return
            if (diff > maxColumn):
                maxColumn = diff

        # Compute the 2-point calibration coefficient.
        diff = (highColumns - lowColumns)
        gain2pt = numpy.full(self.ADC_CHANNELS, maxColumn) / diff

        # Predict the ADC to be linear with dummy voltage and find the error.
        predict = lowColumns + (diff * (vmid - vlow) / (vhigh - vlow))
        err2pt = midColumns - predict

        logging.debug("ADC Columns 2-point gain: %s" % (gain2pt))
        logging.debug("ADC Columns 2-point error: %s" % (err2pt))

        # Add a parabola to compensate for the curvature. This parabola should have
        # zeros at the high and low measurement points, and a curvature to compensate
        # for the error at the middle range. Such a parabola is therefore defined by:
        #
        #  f(X) = a*(X - Xlow)*(X - Xhigh), and
        #  f(Xmid) = -error
        #
        # Solving for the curvature gives:
        #
        #  a = error / ((Xmid - Xlow) * (Xhigh - Xmid))
        #
        # The resulting 3-point calibration function is therefore:
        #
        #  Y = a*X^2 + (b - a*Xhigh - a*Xlow)*X + c
        #  a = three-point curvature correction.
        #  b = two-point gain correction.
        #  c = some constant (black level).
        curve3pt = err2pt / ((midColumns - lowColumns) * (highColumns - midColumns))
        gain3pt = gain2pt - curve3pt * (highColumns + lowColumns)

        logging.debug("ADC Columns 3-point gain: %s" % (gain3pt))
        logging.debug("ADC Columns 3-point curve: %s" % (curve3pt))

        # Load and enable the 3-point calibration.
        colGainRegs = pychronos.fpgamap(pychronos.FPGA_COL_GAIN_BASE, 0x1000)
        colCurveRegs = pychronos.fpgamap(pychronos.FPGA_COL_CURVE_BASE, 0x1000)
        gain3pt *= (1 << self.COL_GAIN_FRAC_BITS)
        curve3pt *= (1 << self.COL_CURVE_FRAC_BITS)
        for col in range(self.MAX_HRES):
            curvature = int(curve3pt[col % self.ADC_CHANNELS])
            colGainRegs.mem16[col] = int(gain3pt[col % self.ADC_CHANNELS])
            if (curvature > 0):
                colCurveRegs.mem16[col] = curvature
            else:
                colCurveRegs.mem16[col] = (0x10000 + curvature) & 0xffff
        display = pychronos.display()
        display.gainControl |= display.GAINCTL_3POINT

    def startAnalogCal(self):     
        # Retrieve the current resolution and frame period.
        fSizePrev = self.getCurrentGeometry()
        fSizeCal = copy.deepcopy(fSizePrev)
        fPeriod = self.timing.frameTime / self.LUX1310_TIMING_HZ

        # Enable black bars if not already done.
        if (fSizeCal.vDarkRows == 0):
            logging.debug("Enabling dark pixel readout")
            fSizeCal.vDarkRows = self.MAX_VDARK // 2
            fSizeCal.vOffset += fSizeCal.vDarkRows
            fSizeCal.vRes -= fSizeCal.vDarkRows

            # Disable the FPGA timing engine and apply the changes.
            self.timing.stopTiming(waitUntilStopped=True)
            time.sleep(0.01) # Extra delay to allow frame readout to finish. 
            self.updateReadoutWindow(fSizeCal)
            self.timing.continueTiming()
        
        # Perform ADC offset calibration using the optical black regions.
        logging.debug('Starting ADC offset calibration')
        yield from self.autoAdcOffsetCal(fSizeCal)

        # Perform ADC column gain calibration using the dummy voltage.
        logging.debug('Starting ADC gain calibration')
        yield from self.autoAdcGainCal(fSizeCal)

        # Restore the frame period and wavetable.
        logging.debug("Restoring sensor configuration")
        self.timing.stopTiming(waitUntilStopped=True)
        time.sleep(0.01) # Extra delay to allow frame readout to finish. 
        self.updateReadoutWindow(fSizePrev)
        self.updateWavetable(fSizePrev, frameClocks=self.timing.frameTime, gaincal=False)
        self.timing.continueTiming()
        

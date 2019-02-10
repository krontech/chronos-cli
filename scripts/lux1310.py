#Luxima LUX1310 Image Sensor Class
import pychronos
import time
import math
import copy
import numpy

from sensor import sensor, frameGeometry
import lux1310wt

class lux1310(sensor):
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

    def __init__(self):
        ## Hardware Resources
        self.sci = pychronos.lux1310()
        self.fpga = pychronos.sensor()
        self.wt = lux1310wt.wavetables

        ## ADC Calibration state
        self.adcOffsets = [0] * self.HRES_INCREMENT

        super().__init__()

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

    def getPeriodRange(self, size):
        # TODO: Need to validate the frame size.
        # TODO: Probably need to enforce some maximum frame period.
        clocks = self.__getMinFrameClocks(size)
        return (clocks / self.LUX1310_TIMING_HZ, 0)
    
    def getExposureRange(self, size):
        # TODO: Does this also need the frame period or can we abstract
        # that away somehow with something like shutter angle and overhead
        # like the old D-Bus daemon docs did.

        # Defaulting to 1us minimum exposure and infinite maximum exposure.
        return (1.0 / 1000000, 0)

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
    
    ## TODO: I think this whole function is unnecessary on the LUX1310 FPGA
    ## builds, even in the C++ camApp it doesn't run to completion, and the
    ## return codes are ignored.
    def __autoPhaseCal(self):
        """Private function - calibrate the FPGA data acquisition channels"""
        self.fpga.clkPhase = 0
        self.fpga.clkPhase = 1
        self.fpga.clkPhase = 0

        # Shift through all possible clock phases to see which one matches.
        valid = 0
        for phase in range(0, 16):
            valid = (valid >> 1)
            if (self.fpga.dataCorrect == 0x1FFFF):
                valid |= 0x8000
            
            # Advance the clock phase
            self.fpga.clkPhase = phase

        if (valid == 0):
            # No valid data window
            print("Phase calibration failed: no data valid window")
            self.fpga.clkPhase = 4
            return False
        
        # Determine the start and length of the window of clock phase values that produce valid outputs
        valid = valid | (valid << 16)
        bestMargin = 0
        bestStart = 0
        for phase in range(0, 16):
            margin = 0
            if (valid & (1 << phase)):
                # Scan starting at this valid point
                for x in range(phase, phase+16):
                    # Track the margin until we hit a non-valid point.
                    if (valid & (1 << x)):
                        margin += 1
                    elif (margin > bestMargin):
                        bestMargin = margin
                        bestStart = phase
        
        if (bestMargin < 3):
            # Insufficient data valid window
            print("Phase calibration failed: insufficient data valid window")
            return False
        
        # Set the clock phase to the best
        self.fpga.clkPhase = (bestStart + bstMargin // 2) % 16
        print("Phase calibration window start: %s" % (bestStart))
        print("Phase calibration window length: %s" % (bestMargin))
        print("Phase calibration clock phase: %s" % (self.fpga.clkPhase))
        return True
    
    def __getMinFrameClocks(self, size, wtSize=0):
        # Select the longest wavetable that fits within the line readout time,
        # or fall back to the shortest wavetable for extremely small resolutions.
        if (wtSize == 0):
            wtIdeal = (size.hRes / self.HRES_INCREMENT) + self.LUX1310_MIN_HBLANK - 3
            for x in self.wt:
                wtSize = x.clocks
                if (wtSize <= wtIdeal):
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

    def __updateWavetable(self, size, frameClocks, gaincal=False):
        # Select the appropriate wavetable for the given resolution and period.
        wtBest = None
        for x in self.wt:
            wtBest = x
            wtClocks = self.__getMinFrameClocks(size, x.clocks)
            if (frameClocks >= self.__getMinFrameClocks(size, x.clocks)):
                break
        
        # If a suitable wavetabl exists, then load it.
        if (wtBest):
            self.sci.regTimingEn = False
            self.sci.regRdoutDly = wtBest.clocks
            self.sci.reg[0x7A] = wtBest.clocks
            if (gaincal):
                self.sci.wavetable(wtBest.gaintab)
            else:
                self.sci.wavetable(wtBest.wavetab)
            self.sci.regTimingEn = True
            self.fpga.startDelay = wtBest.abnDelay
            self.fpga.linePeriod = max((size.hRes // self.HRES_INCREMENT) + 2, wtBest.clocks + 3) - 1
        # Otherwise, the frame period was probably too short for this resolution.
        else:
            raise ValueError("Frame period too short, no suitable wavetable found")

    def boot(self, size=None):
        # Disable integration while setup is in progress.
        self.fpga.framePeriod = 100 * 4000
        self.fpga.intTime = 100 * 4100

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
        self.sci.regDclkInv = True      # Invert DCLK output

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

        # Load the default wavetable and enable the timing engine.
        self.sci.wavetable(self.wt[0].wavetab)
        self.sci.regTimingEn = True
        time.sleep(0.01)

        # Start the FPGA timing engine.
        self.fpga.framePeriod = 100*4000
        self.fpga.intTime = 100*3900
        return True

    def __updateReadoutWindow(self, size):
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
        if (not fPeriod):
            fClocks = self.__getMinFrameClocks(size)
        elif ((fPeriod * self.LUX1310_TIMING_HZ) >= self.__getMinFramePeriod(size)):
            fClocks = fPeriod * self.LUX1310_TIMING_HZ
        else:
            raise ValueError("Frame period too short")

        # Disable the FPGA timing engine and wait for the current readout to end.
        self.fpga.intTime = 0
        time.sleep(self.fpga.framePeriod / self.LUX1310_TIMING_HZ)

        # Switch to the desired resolution pick the best matching wavetable.
        self.__updateReadoutWindow(size)
        self.__updateWavetable(size, frameClocks=fClocks)

        # Switch to the minimum frame period and 180-degree shutter after changing resolution.
        self.fpga.framePeriod = fClocks
        self.fpga.intTime = fClocks // 2

    def setFramePeriod(self, fPeriod):
        # TODO: Sanity-check the frame period.
        self.fpga.framePeriod = math.ceil(fPeriod * self.LUX1310_TIMING_HZ)

    def setExposurePeriod(self, expPeriod):
        # TODO: Sanity-check the exposure time.
        self.fpga.intTime = math.ceil(expPeriod * self.LUX1310_TIMING_HZ)
    
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
        
        vsrtb, vrst, vrsth, samp, feedback, sgain = gainConfig[int(gian)]
        self.sci.writeDAC(self.sci.DAC_VRSTB, vrstb)
        self.sci.writeDAC(self.sci.DAC_VSRT, vrst)
        self.sci.writeDAC(self.sci.DAC_VRSTH, vrsth)
        self.sci.regGainSelSamp = samp
        self.sci.regGainSelFb = feedback
        self.sci.regGainBit = sgain
    
    ## TODO: This might make more sense as a global helper function, since we'll
    ## probably use it all over the place for doing calibration. Maybe the super
    ## is a better place to put it? Or is it more of a pychronos thing?
    def __readFromLive(self, hRes, vRes, numFrames=1):
        seq = pychronos.sequencer()
        backup = [seq.liveAddr[0], seq.liveAddr[1], seq.liveAddr[2]]
        frames = [None] * numFrames
        page = 0

        for i in range(0, numFrames):
            # Set all three live buffers to the same address.
            seq.liveAddr[0] = backup[page]
            seq.liveAddr[1] = backup[page]
            seq.liveAddr[2] = backup[page]

            # Wait for the sequencer to being writing to the update live address.
            while (seq.writeAddr != backup[page]): pass

            # Read a frame then switch pages.
            page ^= 1
            frames[i] = pychronos.readframe(backup[page], hRes, vRes)

        ## Restore the live display buffers to normal operation.
        seq.liveAddr[0] = backup[0]
        seq.liveAddr[1] = backup[1]
        seq.liveAddr[2] = backup[2]
        return frames
    
    def __autoAdcOffsetIteration(self, fSize, numFrames=4):
        # Read out the frames and average them together.
        fAverage = numpy.zeros((fSize.vDarkRows * fSize.hRes // self.ADC_CHANNELS, self.ADC_CHANNELS), dtype=numpy.uint32)
        for f in self.__readFromLive(fSize.hRes, fSize.vDarkRows, numFrames):
            fAverage += numpy.reshape(f, (-1, self.ADC_CHANNELS))
        fAverage /= numFrames

        # Train the ADC offsets for a target of Average = Footroom + StandardDeviation
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

    def __autoAdcOffsetCal(self, fSize, iterations=16):
        tRefresh = (self.fpga.framePeriod * 3) / self.LUX1310_TIMING_HZ

        # Clear out the ADC offsets
        for i in range(0, self.ADC_CHANNELS):
            self.adcOffsets[i] = 0
            self.sci.regAdcOs[i] = 0
        
        # Enable ADC calibration and iterate on the offsets.
        self.sci.regAdcCalEn = True
        for i in range(0, iterations):
            time.sleep(tRefresh)
            self.__autoAdcOffsetIteration(fSize)

    def __autoAdcGainCal(self, fSize):
        colGainRegs = pychronos.fpgamap(pychronos.FPGA_COL_GAIN_BASE, 0x1000)
        colCurveRegs = pychronos.fpgamap(pychronos.FPGA_COL_CURVE_BASE, 0x1000)

        # Setup some math constants
        numRows = 64
        tRefresh = (self.fpga.framePeriod * 3) / self.LUX1310_TIMING_HZ
        pixFullScale = (1 << fSize.bitDepth)

        # Disable the FPGA timing engine.
        prev = self.fpga.intTime
        self.fpga.intTime = 0
        time.sleep(tRefresh)

        # Reload the wavetable for gain calibration
        # TODO: We should probably write a variant of this to leave the frame period alone.
        self.__updateWavetable(fSize, frameClocks=self.fpga.framePeriod, gaincal=True)
        self.fpga.intTime = prev

        # Hunting for the dummy voltage range
        maxColumn = pixFullScale
        minColumn = 0
        vhigh = 31
        vlow = 0

        # Search for a dummy voltage high reference point.
        while (vhigh > 0):
            self.sci.regSelVdumrst = vhigh
            time.sleep(tRefresh)
            frame = numpy.reshape(self.__readFromLive(fSize.hRes, numRows)[0], (-1, self.ADC_CHANNELS))
            
            # Compute the column averages and find the maximum value
            highColumns = numpy.average(frame, 0)
            maxColumn = numpy.amax(frame)

            # High voltage should be less than 7/8ths of full scale.
            if (maxColumn <= (pixFullScale - (pixFullScale / 8))):
                break
            else:
                vhigh -= 1
        
        # Search for a dummy voltage low reference point.
        while (vlow < vhigh):
            self.sci.regSelVdumrst = vlow
            time.sleep(tRefresh)
            frame = numpy.reshape(self.__readFromLive(fSize.hRes, numRows)[0], (-1, self.ADC_CHANNELS))

            # Compute the column averages and find the minimum value
            lowColumns = numpy.average(frame, 0)
            minColumn = numpy.amin(frame)

            # Find the minum voltage that does not clip.
            if (minColumn >= self.ADC_FOOTROOM):
                break
            else:
                vlow += 1

        # Sample the midpoint, which should be somewhere around quarter scale.
        vmid = (vhigh + 3*vlow) // 4
        self.sci.regSelVdumrst = vmid
        time.sleep(tRefresh)
        frame = numpy.reshape(self.__readFromLive(fSize.hRes, numRows)[0], (-1, self.ADC_CHANNELS))
        midColumns = numpy.average(frame, 0)

        #print("ADC Columns low (%s): %s" % (vlow, lowColumns))
        #print("ADC Columns mid (%s): %s" % (vmid, midColumns))
        #print("ADC Columns high (%s): %s" % (vhigh, highColumns))

        # Determine which column has the strongest response and sanity-check the gain
        # measurements. If things are out of range, then give up on gain calibration
        # and apply a gain of 1.0 instead.
        maxColumn = 0
        for col in range(0, self.ADC_CHANNELS):
            minrange = (pixFullScale // 16)
            diff = highColumns[col] - lowColumns[col]
            if ((highColumns[col] <= (midColumns[col] + minrange)) or (midColumns[col] <= (lowColumns[col] + minrange))):
                print("Warning! ADC Auto calibration range error")
                for x in range(0, self.MAX_HRES):
                    colGainRegs.mem16[x] = (1 << self.COL_GAIN_FRAC_BITS)
                    colCurveRegs.mem16[x] = 0
                return
            if (diff > maxColumn):
                maxColumn = diff

        # Compute the 2-point calibration coefficient.
        gain2pt = numpy.full(self.ADC_CHANNELS, maxColumn) / (highColumns - lowColumns)

        # Predict the ADC to be linear with dummy voltage and find the error.
        predict = lowColumns + (diff * (vmid - vlow) / (vhigh - vlow))
        err2pt = midColumns - predict

        #print("ADC Columns 2-point gain: %s" % (gain2pt))
        #print("ADC Columns 2-point error: %s" % (err2pt))

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

        #print("ADC Columns 3-point gain: %s" % (gain3pt))
        #print("ADC Columns 3-point curve: %s" % (curve3pt))

        # Load and enable the 3-point calibration.
        colGainRegs = pychronos.fpgamap(pychronos.FPGA_COL_GAIN_BASE, 0x1000)
        colCurveRegs = pychronos.fpgamap(pychronos.FPGA_COL_CURVE_BASE, 0x1000)
        gain3pt *= (1 << self.COL_GAIN_FRAC_BITS)
        curve3pt *= (1 << self.COL_CURVE_FRAC_BITS)
        for col in range(self.MAX_HRES):
            colGainRegs.mem16[col] = int(gain3pt[col % self.ADC_CHANNELS])
            colCurveRegs.mem16[col] = int(curve3pt[col % self.ADC_CHANNELS]) & 0xffff
        display = pychronos.display()
        display.gainControl |= display.GAINCTL_3POINT

    def autoAnalogCal(self):     
        # Retrieve the current resolution and frame period.
        fSizePrev = self.getCurrentGeometry()
        fSizeCal = copy.deepcopy(fSizePrev)
        fPeriod = self.fpga.framePeriod / self.LUX1310_TIMING_HZ

        # Enable black bars if not already done.
        if (fSizeCal.vDarkRows == 0):
            fSizeCal.vDarkRows = self.MAX_VDARK // 2
            fSizeCal.vOffset += fSizeCal.vDarkRows
            fSizeCal.vRes -= fSizeCal.vDarkRows

            # Disable the FPGA timing engine and apply the changes.
            prev = self.fpga.intTime
            self.fpga.intTime = 0
            time.sleep(fPeriod * 2)
            self.__updateReadoutWindow(fSizeCal)
            self.fpga.intTime = prev
        
        # Perform ADC offset calibration using the optical black regions.
        self.__autoAdcOffsetCal(fSizeCal)

        # Perform ADC column gain calibration using the dummy voltage.
        self.__autoAdcGainCal(fSizeCal)

        # Restore the frame period and wavetable.
        prev = self.fpga.intTime
        self.fpga.intTime = 0
        time.sleep(fPeriod * 2)
        self.__updateReadoutWindow(fSizePrev)
        self.__updateWavetable(fSizePrev, frameClocks=self.fpga.framePeriod, gaincal=False)
        self.fpga.intTime = prev

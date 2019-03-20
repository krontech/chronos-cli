import pychronos
import time
import logging
from regmaps import ioInterface

class sensorTimingProgram(pychronos.fpgamap):
    def __init__(self):
        super().__init__(0x6110, 0x400)
        self.lastIndex = 0

    def __getitem__(self, key):
        self.lastIndex = key
        return self.mem32[key]
    
    def __setitem__(self, key, value):
        self.lastIndex = key
        if value > 0xFFFFFFFF:
            value = 0
            logging.error("====================  ValueError while setting sensorTimingProgram")
        self.mem32[key] = value

    @property
    def next(self):
        self.lastIndex += 1
        return self.mem32[self.lastIndex]

    @next.setter
    def next(self, value):
        self.lastIndex += 1
        if value > 0xFFFFFFFF:
            value = 0
            logging.error("====================  ValueError while setting sensorTimingProgram")
        self.mem32[self.lastIndex] = value
        

class sensorTiming(pychronos.fpgamap):
    ABN         = 0x01000000
    ABN2        = 0x02000000
    TXN         = 0x08000000
    PRSTN       = 0x04000000
    IODRIVE     = 0x10000000
    NONE        = 0x00000000
    
    TIMING_RESTART           = 0x00000000
    TIMING_WAIT_FOR_ACTIVE   = 0x00FFFFFF
    TIMING_WAIT_FOR_INACTIVE = 0x00FFFFFE
    TIMING_WAIT_FOR_NLINES   = 0x00FFFFFC

    TIMING_HZ = 90000000

    PROGRAM_STANDARD       = 0
    PROGRAM_SHUTTER_GATING = 1
    PROGRAM_2POINT_HDR     = 2
    PROGRAM_3POINT_HDR     = 3
    PROGRAM_FRAME_TRIG     = 4
    PROGRAM_N_FRAME_TRIG   = 5
    
    def __init__(self, wt_length=80, fps=1000):
        super().__init__(0x6100, 0x500)
        self.program = sensorTimingProgram()

        # make sure some sane values are in the internal backing
        # registers in case the user just sets the integration and frame times
        self.__frameTime        = 90000 # 1ms
        self.__integrationTime  = 85000 # just less than 1ms
        self.__t2Time           = 17
        self.__disableFrameTrig = False
        self.__disableIoDrive   = False
        self.__nFrames          = 5
        
        self.io = ioInterface.ioInterface()

        if self.version_reg >= 0 and self.subversion_reg >= 2:
            self.useMinLinesWait = True
        else:
            self.useMinLinesWait = False
            
    #-------------------------------------------------
    # property helpers
    def setPropertyBits(self, offset, size, bitOffset, nBits, value):
        self.regWrite(offset, size, (self.regRead(offset, size) & ~((2**nBits-1)<<bitOffset) |
                                     ((value & (2**nBits-1)) << bitOffset)))

    def __regprop(offset, size, docstring):
        return property(fget=lambda self: self.regRead(offset, size),
                        fset=lambda self, value: self.regWrite(offset, size, value),
                        doc = docstring)
    def __regprop_ro(offset, size, docstring):
        return property(fget=lambda self: self.regRead(offset, size),
                        doc = docstring)
    def __bitprop(offset, size, bitOffset, nBits, docstring):
        return property(fget=lambda self: ((self.regRead(offset, size) >> bitOffset) & (2**nBits-1)),
                        fset=lambda self, value: self.setPropertyBits(offset, size, bitOffset, nBits, value),
                        doc = docstring)
    def __bitprop_ro(offset, size, bitOffset, nBits, docstring):
        return property(fget=lambda self: ((self.regRead(offset, size) >> bitOffset) & (2**nBits-1)),
                        doc = docstring)

    #-------------------------------------------------
    # direct access properties
    identifier          = __regprop_ro(0x00, 2, 'ID to make sure the module is there')
    version_reg         = __regprop_ro(0x02, 2, 'whole number portion of version')
    subversion_reg      = __regprop_ro(0x04, 2, 'decimal portion of version')
    
    pulsedAbnLowPeriod  = __regprop(0x0A, 2, 'Pulsed-ABN low period')
    pulsedAbnHighPeriod = __regprop(0x0C, 2, 'Pulsed-ABN high period')

    minLines            = __regprop(0x0E, 2, '''If WAIT_FOR_N_LINES active in a program, this sets how many lines or wavetable periods before the program continues.
    This is used for shutter-gating to make sure it doesn't start a new frame while the last one is being read out''')
    
    #-------------------------------------------------
    status                      = __regprop_ro(0x06, 2, 'status')
    inUseControlPage            = __bitprop_ro(0x06, 2, 3, 1, 'which "page" in the control memory is currently active - the other one is being edited')
    exposureIsEnabled           = __bitprop_ro(0x06, 2, 6, 1, 'if "1" either exposureEnabled is 1 or external shutter signal is enabled')
    frameRequestNotDone         = __bitprop_ro(0x06, 2, 7, 1, 'when requestFrame called, this goes high until the request is completed')
    currentlyWaitingForActive   = __bitprop_ro(0x06, 2, 8, 1, 'if "1" the timing generator is currently waiting for active signal level')
    currentlyWaitingForInactive = __bitprop_ro(0x06, 2, 9, 1, 'if "1" the timing generator is currently waiting for inactive signal level')
    pageSwapState               = __bitprop_ro(0x06, 2, 12, 4, 'state of the state machine that runs the copy on page flip')


    control             = __regprop(0x08, 2, 'control register')
    inhibitTiming       = __bitprop(0x08, 2,  0, 1, 'this enables the new timing engine core')
    requestFlip         = __bitprop(0x08, 2,  1, 1, 'indicates to the engine to flip on next reset or end of frame; self-clears')
    resetSignal         = __bitprop(0x08, 2,  2, 1, 'rising edge resets the internals')
    exposureEnabled     = __bitprop(0x08, 2,  4, 1, 'if enabled, timing engine will wait until exposure signal (level sensitive) or requestFrame (edge sensitive) goes high')
    exposure            = __bitprop(0x08, 2,  5, 1, 'if exposureEnabled is set, this enables the timing engine. If exposureEnabled is clear, this bit is ignored')
    requestFrame        = __bitprop(0x08, 2,  7, 1, 'if exposureEnabled is set, this will request a single frame from the timing engine')
    useAbnPulsedMode    = __bitprop(0x08, 2,  8, 1, 'use ABN pulsed mode')
    invertAbnPulsedMode = __bitprop(0x08, 2,  9, 1, 'invert the ABN signal when in pulsed mode')
    wavetableLatch      = __bitprop(0x08, 2, 10, 1, 'causes change to happen only on hsync period between wavetables')


    

    @property
    def busy(self):
        if (self.pageSwapState == 0x1): return False
        else:                           return True


    @property
    def enabled(self):
        return not self.inhibitTiming
    @enabled.setter
    def enabled(self, value):
        if value: self.inhibitTiming = 0
        else:     self.inhibitTiming = 1
        
    #-------------------------------------------------
    # operations
    def reset(self):
        self.resetSignal = 1
        # reset is edge-sensitive only so clearing the bit right away
        # is the best way to use it
        self.resetSignal = 0

    def forcedFlip(self):
        self.requestFlip = 1
        self.reset()
    def normalFlip(self):
        if (not self.busy):
            self.requestFlip = 1
        else:
            logging.error("Error: sensorTiming flip state machine still busy")
        
    def flip(self, timeout=0.01, force=False):
        origShutterTriggersFrame = self.io.shutterTriggersFrame
        if self.currentlyWaitingForActive or self.currentlyWaitingForInactive:
            logging.info('flip while waiting for external IO - disabling IO during flip')
            self.io.shutterTriggersFrame = False
            self.exposureEnabled = 0
        
        if timeout < 0:
            logging.info('---- Forcing flip')
            self.forcedFlip()
        else:
            start = time.time()
            while time.time() < (start + timeout):
                if not self.busy:
                    break
            if self.busy:
                logging.error('---- Forcing flip')
                self.forcedFlip()
            else:
                self.normalFlip()

        start = time.time()
        while time.time() < (start + timeout):
            if not self.busy:
                break
        self.io.shutterTriggersFrame = origShutterTriggersFrame
    
    def setPulsedPattern(self, wavetableLength, hSync=2):
        self.pulsedAbnLowPeriod = wavetableLength
        self.pulsedAbnHighPeriod = hSync

    def stopTiming(self, waitUntilStopped=False, timeout=0.0):
        self.programInterm()
        #self.exposureEnabled = 1
        #if waitUntilStopped:
        #    if timeout == 0.0:
        #        timeout = 1.5 * self.frameTime / self.TIMING_HZ
        #    start = time.time()
        #    while time.time() < (start + timeout):
        #        if self.currentlyWaitingForActive or self.currentlyWaitingForInactive:
        #            break
        #    if not self.currentlyWaitingForActive or self.currentlyWaitingForInactive:
        #        logging.warning('timing engine did not stop as expected')
        
    def singleFrame(self):
        self.requestFrame = 1

    def continueTiming(self):
        self.programLast()
        #self.exposureEnabled = 0

        
    @property
    def frameTime(self):
        logging.info("frameTime: %d", self.__frameTime)
        return self.__frameTime
    @frameTime.setter
    def frameTime(self, value):
        if self.__program == self.PROGRAM_STANDARD:
            self.programStandard(value, self.__integrationTime, self.__t2Time, self.__disableFrameTrig, self.__disableIoDrive)
        elif self.__program == self.PROGRAM_FRAME_TRIG:
            self.programTriggerFrames(value, self.__integrationTime, self.__t2Time, self.__disableIoDrive)
        elif self.__program == self.PROGRAM_N_FRAME_TRIG:
            self.programTriggerNFrames(value, self.__integrationTime, self.__nFrames, self.__t2Time, self.__disableIoDrive)
        elif self.__program == self.PROGRAM_SHUTTER_GATING:
            pass
            
    @property
    def integrationTime(self):
        logging.info("integrationTime: %d", self.__integrationTime)
        return self.__integrationTime
    @integrationTime.setter
    def integrationTime(self, value):
        if self.__program == self.PROGRAM_STANDARD:
            self.programStandard(self.__frameTime, value, self.__t2Time, self.__disableFrameTrig, self.__disableIoDrive)
        elif self.__program == self.PROGRAM_FRAME_TRIG:
            self.programTriggerFrames(self.__frameTime, value, self.__t2Time, self.__disableIoDrive)
        elif self.__program == self.PROGRAM_N_FRAME_TRIG:
            self.programTriggerNFrames(self.__frameTime, value, self.__nFrames, self.__t2Time, self.__disableIoDrive)
        elif self.__program == self.PROGRAM_SHUTTER_GATING:
            pass

    def programInterm(self, readoutTime=90000, timeout=0.01):
        """This programs an blank program which will make sure the readout
        section has enough time to complete readout before switching to a
        new timing program.
        This has all IO disabled including the one driving the io block...
        """
        self.program[0]        = self.NONE + readoutTime;
        self.program.next = self.NONE | self.TIMING_RESTART
        logging.info('programInterm - flip')
        self.flip(timeout)

    def programLast(self):
        if self.__program == self.PROGRAM_STANDARD:
            self.programStandard(self.__frameTime, self.__integrationTime, self.__t2Time, self.__disableFrameTrig, self.__disableIoDrive)
        elif self.__program == self.PROGRAM_FRAME_TRIG:
            self.programTriggerFrames(self.__frameTime, value, self.__t2Time, self.__disableIoDrive)
        elif self.__program == self.PROGRAM_N_FRAME_TRIG:
            self.programTriggerNFrames(self.__frameTime, value, self.__nFrames, self.__t2Time, self.__disableIoDrive)
        elif self.__program == self.PROGRAM_SHUTTER_GATING:
            self.programShutterGating(self.__t2Time)
        else:
            logging.error('unknown last timing program')
        
    def programShutterGating(self, t2Time=17, readoutTime=90000, timeout=0.01):
        self.__program = self.PROGRAM_SHUTTER_GATING
        self.__t2Time           = t2Time
        self.__disableFrameTrig = False

        logging.info('programShutterGating')
        # make sure the readout completes
        self.programInterm(readoutTime, timeout)
        self.io.shutterTriggersFrame = False
        
        self.program[0]   = self.NONE + t2Time
        self.program.next = self.ABN                 | self.TIMING_WAIT_FOR_ACTIVE
        self.program.next = self.IODRIVE | self.NONE | self.TIMING_WAIT_FOR_INACTIVE
        self.program.next = self.TXN  | 0x31
        if self.useMinLinesWait:
            self.program.next = self.TXN | self.TIMING_WAIT_FOR_NLINES
        self.program.next = self.NONE | self.TIMING_RESTART
        
        logging.info('programShutterGating - flip')
        self.flip(timeout)
        self.io.shutterTriggersFrame = True
        
    def programTriggerFrames(self, frameTime, integrationTime, t2Time=17, disableIoDrive=False, readoutTime=90000, timeout=0.01):
        frameTime       = int(frameTime)
        integrationTime = int(integrationTime)
        t2Time          = int(t2Time)
        
        if (frameTime <= integrationTime):
            logging.error("frameTime (%d) must be longer than integrationTime (%d)", frameTime, integrationTime)
            integrationTime = int(frameTime * 0.95)

        logging.info('TriggerFrames: %d, %d', frameTime, integrationTime)
        # make sure the readout completes
        self.programInterm(readoutTime, timeout)
        self.io.shutterTriggersFrame = False

        self.__program          = self.PROGRAM_FRAME_TRIG
        self.__frameTime        = frameTime
        self.__integrationTime  = integrationTime
        self.__t2Time           = t2Time
        self.__disableFrameTrig = False
        self.__disableIoDrive   = disableIoDrive


        if disableIoDrive: ioDrive = 0
        else:              ioDrive = self.IODRIVE

        
        self.program[0] = self.NONE + t2Time                             # period before ABN goes low (must be t2 time)
        # ABN Falls here
        self.program.next = self.ABN | self.TIMING_WAIT_FOR_ACTIVE   # (hence why the hold happens after the first command)
        self.program.next = self.ABN + (frameTime - integrationTime)
        self.program.next = ioDrive + self.NONE + (integrationTime) # ABN raises
        self.program.next = ioDrive + self.TXN + 0x31               # TXN falls
        self.program.next = self.TIMING_RESTART                          # TXN raises and cycle restarts

        logging.info('programTriggerFrames - flip')
        self.flip(timeout)
        self.io.shutterTriggersFrame = True
        
    def programStandard(self, frameTime, integrationTime, t2Time=17, disableFrameTrig=True, disableIoDrive=False, readoutTime=90000, timeout=0.01):
        frameTime       = int(frameTime)
        integrationTime = int(integrationTime)
        t2Time          = int(t2Time)
        if (frameTime <= integrationTime):
            logging.error("frameTime (%d) must be longer than integrationTime (%d)", frameTime, integrationTime)
            integrationTime = int(frameTime * 0.95)

        logging.info('ProgramStandard: %d, %d', frameTime, integrationTime)
        # make sure the readout completes
        self.programInterm(readoutTime, timeout)
        origShutterTriggersFrame = self.io.shutterTriggersFrame
        self.io.shutterTriggersFrame = False

        self.__program          = self.PROGRAM_STANDARD
        self.__frameTime        = frameTime
        self.__integrationTime  = integrationTime
        self.__t2Time           = t2Time
        self.__disableFrameTrig = disableFrameTrig
        self.__disableIoDrive   = disableIoDrive

        if disableIoDrive: ioDrive = 0
        else:              ioDrive = self.IODRIVE
        
        self.program[0] = self.NONE + t2Time                             # period before ABN goes low (must be t2 time)
        if (disableFrameTrig):                                           # ABN Falls here
            self.program.next = self.ABN | self.TIMING_WAIT_FOR_INACTIVE # Timing for the pulsed mode is reset on the falling edge
            self.program.next = self.ABN | self.TIMING_WAIT_FOR_ACTIVE   # (hence why the hold happens after the first command)
        self.program.next = self.ABN + (frameTime - integrationTime)
        self.program.next = ioDrive + self.NONE + (integrationTime) # ABN raises
        #self.program.next = ioDrive + self.PRSTN + 0x000001         # PRSTN falls
        #self.program.next = ioDrive + self.NONE + 0x000016          # and raises
        self.program.next = ioDrive + self.TXN + 0x31               # TXN falls
        self.program.next = self.TIMING_RESTART                          # TXN raises and cycle restarts

        logging.info('programStandard - flip')
        self.flip(timeout)
        self.io.shutterTriggersFrame = origShutterTriggersFrame

                
    def programSpecial(self, frameTime, integrationTime, t2Time, readoutTime=90000, timeout=0.01):
        wavetableTime = self.pulsedAbnHighPeriod + self.pulsedAbnLowPeriod

        preFrameTime = (frameTime - integrationTime) // (wavetableTime+2)
        preFrameTime *= (wavetableTime+2)
        preFrameTime -= 16
        
        logging.info('ProgramSpecial: %d, %d', frameTime, integrationTime)
        # make sure the readout completes
        self.programInterm(readoutTime, timeout)
        origShutterTriggersFrame = self.io.shutterTriggersFrame
        self.io.shutterTriggersFrame = False

        self.program[0]   = self.NONE + t2Time
        self.program.next = self.ABN + self.TIMING_WAIT_FOR_INACTIVE
        self.program.next = self.ABN + self.TIMING_WAIT_FOR_ACTIVE
        self.program.next = self.ABN + preFrameTime - (wavetableTime+2)
        self.program.next = self.ABN + (wavetableTime+2)
        self.program.next = self.IODRIVE + self.NONE + (integrationTime)
        self.program.next = self.IODRIVE + self.TXN + 6
        self.program.next = self.NONE + self.TIMING_RESTART

        logging.info('programSpecial - flip')
        self.flip(timeout)
        self.io.shutterTriggersFrame = origShutterTriggersFrame

    def programHDR_2slope(self, frameTime, integration1, integration2, t2Time=17, VDR1=2.5, VDR2 = 2.0, readoutTime=90000, timeout=0.01):
        if (integration1 + integration2 + t2Time + 50) > frameTime:
            logging.error("frameTime (%d) must be longer than integrationTime (%d)", frameTime, integrationTime)
            integrationTime = frameTime * 0.95

        logging.info('ProgramHDR_2slope: %d, %d, %d', frameTime, integrationTime1, integrationTime2)
        # make sure the readout completes
        self.programInterm(readoutTime, timeout)
        origShutterTriggersFrame = self.io.shutterTriggersFrame
        self.io.shutterTriggersFrame = False

        lux = pychronos.lux1310()
        lux.writeDAC(lux.DAC_VDR1, VDR1) # example 2.5
        lux.writeDAC(lux.DAC_VDR2, VDR2) # example 2.0 or 2.2
        lux.writeDAC(lux.DAC_VDR3, VDR2)

        # r31 = 2
        lux.regFtTrigNbPulse = 2
        # r69 = Tint2 + 640ns = 
        lux.regSelVdr1Width  = integration2 + t2Time + 50
        # r6A = 0
        lux.regSelVdr2Width  = 0
        # r6B = 0
        lux.regSelVdr3Width  = 0
        # r67 = 1
        lux.regHidyEn        = True
        
        #... hmm... need to set up some other stuff too
        self.program[0]   = self.NONE  + t2Time
        self.program.next = self.ABN   + self.TIMING_WAIT_FOR_INACTIVE
        self.program.next = self.ABN   + self.TIMING_WAIT_FOR_ACTIVE
        self.program.next = self.ABN   + 50
        self.program.next = self.NONE  + integration1 - (15+45)
        self.program.next = self.PRSTN + 15                      # PRSTN \__ to TXN \__
        self.program.next = self.PRSTN + self.TXN + 45           # TXN ___
        self.program.next = self.PRSTN + 60                      # TXN __/ to TXN \__
        self.program.next = self.TXN   + (integration2 - 15)
        logging.info('programHDR-2slope - flip')
        self.flip(timeout)
        self.io.shutterTriggersFrame = origShutterTriggersFrame

    def programHDR_3slope(self, frameTime, integration1, integration2, integration3, t2Time=17, VDR1=2.5, VDR2 = 2.0, readoutTime=90000, timeout=0.01):
        if (integration1 + integration2 + integration3 + t2Time + 50) > frameTime:
            raise ValueError("frameTime (%d) must be longer than total integrationTime (%d)" % (frameTime, integrationTime))

        logging.info('ProgramHDR_3slope: %d, %d, %d', frameTime, integrationTime1, integrationTime2, integrationTime3)
        # make sure the readout completes
        self.programInterm(readoutTime, timeout)
        origShutterTriggersFrame = self.io.shutterTriggersFrame
        self.io.shutterTriggersFrame = False

        lux = pychronos.lux1310()
        lux.writeDAC(lux.DAC_VDR1, VDR1) # example 2.5
        lux.writeDAC(lux.DAC_VDR2, VDR2) # example 2.0 or 2.2
        lux.writeDAC(lux.DAC_VDR3, VDR2)

        lux.regFtTrigNbPulse = 3
        lux.regSelVdr1Width  = (15 + 45 + 60)
        lux.regSelVdr2Width  = (integration2 + integration3)
        lux.regSelVdr3Width  = 0
        lux.regHidyEn        = True
        
        #... hmm... need to set up some other stuff too
        self.program[0]   = self.NONE  + t2Time
        self.program.next = self.ABN   + 50
        self.program.next = self.NONE  + integration1 - (15+45)
        self.program.next = self.PRSTN + 15                      # PRSTN \__ to TXN \__
        self.program.next = self.PRSTN + self.TXN + 45           # TXN ___
        self.program.next = self.PRSTN + 60                      # TXN __/ to TXN \__
        self.program.next = self.PRSTN + self.TXN + (integration2 - 60)
        self.program.next = self.PRSTN + 15
        self.program.next = self.TXN   + (integration3 - 15)
        logging.info('programHDR-3slope - flip')
        self.flip(timeout)
        self.io.shutterTriggersFrame = origShutterTriggersFrame


    def programTriggerNFrames(self, frameTime, integrationTime, nFrames=5, t2Time=17, disableIoDrive=False, readoutTime=90000, timeout=0.01):
        frameTime       = int(frameTime)
        integrationTime = int(integrationTime)
        t2Time          = int(t2Time)
        if (frameTime <= integrationTime):
            logging.error("frameTime (%d) must be longer than integrationTime (%d)", frameTime, integrationTime)
            integrationTime = frameTime * 0.95

        logging.info('ProgramTriggerNFrames: %d, %d', frameTime, integrationTime)
        # make sure the readout completes
        self.programInterm(readoutTime, timeout)
        origShutterTriggersFrame = self.io.shutterTriggersFrame
        self.io.shutterTriggersFrame = False

        self.__program          = self.PROGRAM_N_FRAME_TRIG
        self.__frameTime        = frameTime
        self.__integrationTime  = integrationTime
        self.__t2Time           = t2Time
        self.__disableFrameTrig = False
        self.__disableIoDrive   = disableIoDrive
        self.__nFrames          = nFrames

        if disableIoDrive: ioDrive = 0
        else:              ioDrive = self.IODRIVE
        
        self.program[0] = self.NONE + t2Time                             # period before ABN goes low (must be t2 time)
        # ABN Falls here
        self.program.next = self.ABN | self.TIMING_WAIT_FOR_ACTIVE       # (hence why the hold happens after the first command)

        for i in range(nFrames):
            self.program.next = self.ABN + (frameTime - integrationTime)
            self.program.next = ioDrive + self.NONE + (integrationTime)  # ABN raises
            self.program.next = ioDrive + self.TXN  + 0x31               # TXN falls

        self.program.next = self.TIMING_RESTART                          # TXN raises and cycle restarts

        logging.info('programTriggerNFrames - flip')
        self.flip(timeout)
        self.io.shutterTriggersFrame = origShutterTriggersFrame

        
        

if __name__ == '__main__':
    timing = sensorTiming()
    sensor = pychronos.sensor()
    lux    = pychronos.lux1310()
    timing.inhibitTiming = False
    waveTableLength = lux.regRdoutDly
    print ("wavetable: %d" % waveTableLength)
    t2Time = lux.regSofDelay + 3
    print ("t2Time: %d" % t2Time)
    timing.setPulsedPattern(waveTableLength, hSync=1)
    timing.useAbnPulsedMode = False
    
    while (True):
        time.sleep(0.1)
        frameTime       = int(sensor.framePeriod * 0.9)
        integrationTime = int(sensor.intTime * 0.9)

        timing.programStandard(frameTime, integrationTime, t2Time=t2Time)
        #timing.programSpecial(frameTime, integrationTime, t2Time=t2Time)
        #timing.programHDR_3slope(frameTime, int(integrationTime * 0.9), int(integrationTime * 0.09), int(integrationTime * 0.01), VDR1=2.5, VDR2=2.0)
        

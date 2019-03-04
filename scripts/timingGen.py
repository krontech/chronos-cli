from regmaps import ioInterface
from regmaps import sensorTiming
import camera
import pychronos
import time

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

    io = ioInterface()
    io.setConfiguration({
        'io1': {'source':'delay', 'driveStrength':3, 'invert':False},
        'io2': {'source':'alwaysHigh', 'driveStrength':3},
        'delay': {'source':'timingIo', 'delay':0.0},
        'shutter': {'source':'io2', 'shutterTriggersFrame':True, 'invert':True},
        'io2In': {'threshold':1.5}
    })
    
    time.sleep(0.1)
    frameTime       = int(sensor.framePeriod * 0.9)
    integrationTime = int(sensor.intTime * 0.9)

    timing.programTriggerFrames(frameTime, integrationTime, t2Time=t2Time, timeout=0.1)

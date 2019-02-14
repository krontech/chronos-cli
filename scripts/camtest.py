#!/usr/bin/python3
from camera import camera
from lux1310 import lux1310
from sequencer import seqcommand

import os
import time
import select
import subprocess
import json

# Some constants that ought to go into a board-specific dict.
FPGA_BITSTREAM = "/var/camera/FPGA.bit"
GPIO_ENCA = "/sys/class/gpio/gpio20/value"
GPIO_ENCB = "/sys/class/gpio/gpio26/value"
GPIO_ENCSW = "/sys/class/gpio/gpio27/value"
REC_LED_FRONT = "/sys/class/gpio/gpio41/value"
REC_LED_BACK = "/sys/class/gpio/gpio25/value"

# Initialize the Camera and image sensor
cam = camera(lux1310())
cam.reset(FPGA_BITSTREAM)

# Perform initial calibration
for delay in cam.sensor.startAnalogCal():
    time.sleep(delay)
for delay in cam.startZeroTimeBlackCal():
    time.sleep(delay)

# Grab the current frame size
fSize = cam.sensor.getCurrentGeometry()
recState = None

# Quick and dirty class to help out with GPIO access
class gpio:
    def __init__(self, path):
        self.fd = os.open(path, os.O_RDWR)
        self.value = self.__read()
        self.prev = self.value
    
    def __read(self):
        os.lseek(self.fd, 0, os.SEEK_SET)
        buf = os.read(self.fd, 2)
        return (buf[0] == b"1"[0])
    
    def update(self):
        self.prev = self.value
        self.value = self.__read()
        return self.value
    
    def changed(self):
        return self.prev != self.value

    def rising(self):
        return self.value and not self.prev
    
    def falling(self):
        return self.prev and not self.value

# Setup GPIO pins to read the encoder wheel
enca = gpio(GPIO_ENCA)
encb = gpio(GPIO_ENCB)
encsw = gpio(GPIO_ENCSW)

## Read the encoder wheel for instructions.
p = select.epoll()
p.register(enca.fd, select.EPOLLIN | select.EPOLLET | select.EPOLLPRI)
p.register(encb.fd, select.EPOLLIN | select.EPOLLET | select.EPOLLPRI)
p.register(encsw.fd, select.EPOLLIN | select.EPOLLET | select.EPOLLPRI)
while True:
    p.poll(-1)

    delta = 0
    enca.update()
    encb.update()
    encsw.update() # TODO: Debounce Me!

    # On encoder motion, change the exposure timing.
    if (encb.changed() and not enca.value):
        fPeriod = cam.sensor.getCurrentPeriod()
        exposure = cam.sensor.getCurrentExposure()
        expMin, expMax = cam.sensor.getExposureRange(fSize)
        if ((expMax == 0) or (expMax > fPeriod)):
            expMax = fPeriod
        
        # Encoder decrease
        if encb.rising():
            exposure -= (expMax - expMin) / 10
            if (exposure < expMin):
                exposure = expMin
        # Encoder increase
        if encb.falling():
            exposure += (expMax - expMin) / 10
            if (exposure > expMax):
                exposure = expMax
        
        # Apply the exposure change.
        try:
            print("Setting exposure to %s" % (exposure))
            cam.sensor.setExposurePeriod(exposure)
        except:
            print("Unable to set exposure")
    
    # Start recording on encoder switch depress
    if (encsw.rising()):
        # Start recording in gated burst.
        program = [ seqcommand(blockSize=cam.getRecordingMaxFrames(fSize), blkTermRising=True) ]
        print("Starting recording with program=%s" % (program))
        recState = cam.startRecording(program)
        os.system("cam-json -v flush")

        # Turn on the record LEDs (active low)
        f = open(REC_LED_FRONT, "w")
        f.write("0")
        f = open(REC_LED_BACK, "w")
        f.write("0")

        # TODO: Track the recording state using epoll timeouts.
    
    # End recording and begin filesave on encoder switch release.
    if (encsw.falling()):
        cam.stopRecording()
        time.sleep(1)
        print("Terminating recording")

        # Turn off the record LEDs (active low)
        f = open(REC_LED_FRONT, "w")
        f.write("1")
        f = open(REC_LED_BACK, "w")
        f.write("1")
        break

# Get some info on how much was recorded.
status = json.loads(subprocess.check_output(["cam-json", "-v", "status"]).decode("utf-8"))
if (status['totalFrames'] == 0):
    print("No frames recorded - aborting")
    print("Status = %s" % (status))

# Save the resulting recording to a file.
filesaveArgs = {
    "filename": os.path.join(os.getcwd(), "test.mp4"),
    "format": "x264",
    "start": 0,
    "length": status['totalFrames'],
    "framerate": 60,
    "bitrate": int(fSize.pixels() * 60 * 0.5)
}
subprocess.check_output(["cam-json", "-v", "recordfile", "-"], input=json.dumps(filesaveArgs).encode())

# Poll the status API until recording is completed.
while True:
    status = json.loads(subprocess.check_output(["cam-json", "-v", "status"]).decode("utf-8"))
    if ("filename" in status):
        print("Writing frame %s of %s at %s fps" % (status['position'], status['totalFrames'], status['framerate']))
    else:
        break

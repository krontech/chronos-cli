#!/usr/bin/python3
from camera import camera
from lux1310 import lux1310

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

# Initialize the Camera and image sensor
cam = camera(lux1310())
cam.reset(FPGA_BITSTREAM)

# Perform initial calibration
for delay in cam.sensor.startAnalogCal():
    time.sleep(delay)
for delay in cam.startZeroTimeBlackCal():
    time.sleep(delay)

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
    p.poll()

    delta = 0
    enca.update()
    encb.update()
    encsw.update() # TODO: Debounce Me!
    fSize = cam.sensor.getCurrentGeometry()

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
        print("TODO: Start Recording in gated burst mode")
    
    # End recording and begin filesave on encoder switch release.
    # TODO: Need to disable the encoder switch until save is done,
    # poll the status API until it it's not saving anymore.
    if (encsw.falling()):
        filesaveArgs = {
            "filename": os.path.join(os.getcwd(), "test.mp4"),
            "format": "x264",
            "start": 0,
            "framerate": 60,
            "bitrate": int(fSize.pixels() * 60 * 0.5)
        }
        status = subprocess.check_output(["cam-json", "-v", "recordfile", "-"], input=json.dumps(filesaveArgs).encode())
        print(status)

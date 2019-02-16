#!/usr/bin/python
API_VERISON_STRING = '0.1'

import numpy

from twisted.internet import reactor, defer, utils
from twisted.internet.defer import inlineCallbacks

from txdbus           import client, objects, error
from txdbus.interface import DBusInterface, Method, Signal
from txdbus.objects   import dbusMethod

import logging

from camera import camera
from sensors import lux1310, frameGeometry
from regmaps import seqcommand
import pychronos

from ioInterface import ioInterface

def asleep(secs):
    """
    @brief Do a reactor-safe sleep call. Call with yield to block until done.
    @param secs Time, in seconds
    @retval Deferred whose callback will fire after time has expired
    """
    d = defer.Deferred()
    reactor.callLater(secs, d.callback, None)
    return d

#-----------------------------------------------------------------
# Some constants that ought to go into a board-specific dict.
FPGA_BITSTREAM = "/var/camera/FPGA.bit"
GPIO_ENCA = "/sys/class/gpio/gpio20/value"
GPIO_ENCB = "/sys/class/gpio/gpio26/value"
GPIO_ENCSW = "/sys/class/gpio/gpio27/value"
REC_LED_FRONT = "/sys/class/gpio/gpio41/value"
REC_LED_BACK = "/sys/class/gpio/gpio25/value"
#-----------------------------------------------------------------

krontechControl     =  'com.krontech.chronos.control'
krontechControlPath = '/com/krontech/chronos/control'





class controlApi(objects.DBusObject):
    iface = DBusInterface(krontechControl,
                          Method('getCameraData',            arguments='',      returns='a{sv}'),
                          Method('getSensorData',            arguments='',      returns='a{sv}'),
                          Method('status',                   arguments='',      returns='a{sv}'),
                          Signal('statusHasChanged',         arguments='a{sv}'),

                          Method('reinitSystem',             arguments='a{sv}', returns='a{sv}'),

                          Method('getSensorCapabilities',    arguments='',      returns='a{sv}'),
                          Method('getSensorSettings',        arguments='',      returns='a{sv}'),
                          Method('getSensorLimits',          arguments='a{sv}', returns='a{sv}'),
                          Method('setSensorSettings',        arguments='a{sv}', returns='a{sv}'),

                          Method('getIoCapabilities',        arguments='',      returns='a{sv}'),
                          Method('getIoMapping',             arguments='',      returns='a{sv}'),
                          Method('setIoMapping',             arguments='a{sv}', returns='a{sv}'),
                          Signal('ioEvent',                  arguments='a{sv}'),
                          
                          Method('getCalCapabilities',       arguments='',      returns='a{sv}'),
                          Method('calibrate',                arguments='a{sv}', returns='a{sv}'),

                          Method('getSequencerCapabilities', arguments='',      returns='a{sv}'),
                          Method('getSequencerProgram',      arguments='a{sv}', returns='a{sv}'),
                          Method('setSequencerProgram',      arguments='a{sv}', returns='a{sv}'),
                          Method('startRecord',              arguments='a{sv}', returns='a{sv}'),
                          Method('stopRecord',               arguments='a{sv}', returns='a{sv}'),
    )
    dbusInterfaces = [iface]

    ERROR_NOT_IMPLEMENTED_YET = 9999

   
    
    def __init__(self, objectPath, conn, camera):
        self.conn = conn
        super().__init__(objectPath)
        self.currentState = 'idle'

        self.camera = camera
        self.io = ioInterface() 

        self.reinitSystem({'reset':True, 'sensor':True})


    def emitStateChanged(self, reason=None, details=None):
        data = {'state':self.currentState}
        if reason:
            data['reason'] = reason
        if details:
            data['details'] = details
        self.emitSignal('statusHasChanged', (data))
        

    def pokeCamPipelineToRestart(self):
        utils.getProcessOutput('killall', ['-HUP', 'cam-pipeline'])

    #===============================================================================================
    #Method('getCameraData',            arguments='',      returns='a{sv}'),
    #Method('getSensorData',            arguments='',      returns='a{sv}'),
    #Method('status',                   arguments='',      returns='a{sv}'),
    #Signal('statusHasChanged',         arguments=''),
    
    @dbusMethod(krontechControl, 'getCameraData')
    def dbusGetCameraData(self):
        return {
            'model':'Chronos1.4',
            'serial':0, # make this get hardware serial
            'apiVersion':API_VERISON_STRING,
            'serial':self.camera.getSerialNumber().strip()
        }

    @dbusMethod(krontechControl, 'getSensorData')
    def dbusGetSensorData(self):
        return {
            'name':'LUX1310',
            'pixelRate':1.4*10**9,
            'hMax':1280,
            'vMax':1024,
            'hMin':320,
            'vMin':2,
            'hIncrement':16,
            'vIncrement':2,
            'pixelFormat':'BYR2-RGGB'
        }

    @dbusMethod(krontechControl, 'status')
    def dbusStatus(self):
        logging.debug('request for status')
        return {'state':self.currentState}
    
    #===============================================================================================
    #Method('reinitSystem',             arguments='a{sv}', returns='a{sv}'),
    
    @dbusMethod(krontechControl, 'reinitSystem')
    def dbusReinitSystem(self, args):
        reactor.callLater(0.0, self.reinitSystem, args)
        self.currentState = 'reinitializing'
        return {'state':self.currentState}

    def reinitSystem(self, args):
        self.emitStateChanged()
        reinitAll = args.get('all', False)
        if args.get('fpga') or reinitAll:
            reinitAll = True
            self.camera.reset(FPGA_BITSTREAM)

        if args.get('reset'):
            self.camera.reset()
            
        if args.get('sensor') or reinitAll:
            self.camera.sensor.reset()

        self.currentState = 'idle'
        self.emitStateChanged(reason='(re)initialization complete')

    #===============================================================================================
    #Method('getSensorCapabilities',    arguments='',      returns='a{sv}'),
    #Method('getSensorSettings',        arguments='',      returns='a{sv}'),
    #Method('getSensorLimits',          arguments='a{sv}', returns='a{sv}'),
    #Method('setSensorSettings',        arguments='a{sv}', returns='a{sv}'),
    
    @dbusMethod(krontechControl, 'getSensorCapabilities')
    def dbusGetSensorCapabilities(self):
        return {'failed':'Not Implemented Yet - poke otter', 'id':self.ERROR_NOT_IMPLEMENTED_YET}

    @dbusMethod(krontechControl, 'getSensorSettings')
    def dbusGetSensorSettings(self):
        returnGeom = dict(vars(self.camera.sensor.getCurrentGeometry()))
        returnGeom['framePeriod'] = self.camera.sensor.getCurrentPeriod()
        returnGeom['frameRate']   = 1.0 / returnGeom['framePeriod']
        returnGeom['exposure']    = self.camera.sensor.getCurrentExposure()
        return returnGeom

    @dbusMethod(krontechControl, 'getSensorLimits')
    def dbusGetSensorLimits(self, args):
        minPeriod, maxPeriod = self.camera.sensor.getPeriodRange(self.camera.sensor.getCurrentGeometry())
        return {'minPeriod':minPeriod, 'maxPeriod':maxPeriod}

    @dbusMethod(krontechControl, 'setSensorSettings')
    def setSensorSettings(self, args):
        geom = self.camera.sensor.getCurrentGeometry()
        geom.hRes      = args.get('hRes',      geom.hRes)
        geom.vRes      = args.get('vRes',      geom.vRes)
        geom.hOffset   = args.get('hOffset',   geom.hOffset)
        geom.vOffset   = args.get('vOffset',   geom.vOffset)
        geom.vDarkRows = args.get('vDarkRows', geom.vDarkRows)
        geom.bitDepth  = args.get('bitDepth',  geom.bitDepth)

        # set default value
        framePeriod, _ = self.camera.sensor.getPeriodRange(geom)

        # check if we have a frameRate field and if so convert it to framePeriod
        frameRate = args.get('frameRate')
        if frameRate:
            framePeriod = 1.0 / frameRate

        # if we have a framePeriod explicit field, override frameRate or default value
        framePeriod = args.get('framePeriod', framePeriod)

        # set exposure or use a default of 95% framePeriod
        exposurePeriod = args.get('exposure', framePeriod * 0.95) # self.camera.sensor.getExposureRange(geom))

        # set up video
        self.camera.sensor.setResolution(geom)
        self.camera.setupDisplayTiming(geom)
        self.camera.sensor.setFramePeriod(framePeriod)
        self.camera.sensor.setExposurePeriod(exposurePeriod)

        # tell video pipeline to restart
        reactor.callLater(0.0, self.pokeCamPipelineToRestart)

        # start a calibration loop
        reactor.callLater(0.0, self.startCalibration, {'analog':True, 'zeroTimeBlackCal':True})

        # get the current config so we can return the real values
        appliedGeometry = self.dbusGetSensorSettings()
        reactor.callLater(0.0, self.emitStateChanged, reason='resolution changed', details={'geometry':appliedGeometry})
        return appliedGeometry

    #===============================================================================================
    #Method('getIoCapabilities',        arguments='',      returns='a{sv}'),
    #Method('getIoMapping',             arguments='a{sv}', returns='a{sv}'),
    #Method('setIoMapping',             arguments='a{sv}', returns='a{sv}'),
    #Signal('ioEvent',                  arguments='a{sv}'),

    @dbusMethod(krontechControl, 'getIoCapabilities')
    def dbusGetIoCapabilities(self):
        return {'failed':'Not Implemented Yet - poke otter', 'id':self.ERROR_NOT_IMPLEMENTED_YET}

    @dbusMethod(krontechControl, 'getIoMapping')
    def dbusGetIoMapping(self, args):
        return self.io.getConfiguration()

    @dbusMethod(krontechControl, 'setIoMapping')
    def dbusSetIoMapping(self, args):
        self.io.setConfiguration(args)
        return self.io.getConfiguration()

    #===============================================================================================
    #Method('getCalCapabilities',       arguments='',      returns='a{sv}'),
    #Method('calibrate',                arguments='a{sv}', returns='a{sv}'),
    
    @dbusMethod(krontechControl, 'getCalCapabilities')
    def dbusGetCalCapabilities(self):
        return {'failed':'Not Implemented Yet - poke otter', 'id':self.ERROR_NOT_IMPLEMENTED_YET}

    @dbusMethod(krontechControl, 'calibrate')
    def dbusCalibrate(self, args):
        reactor.callLater(0.0, self.startCalibration, args)
        self.currentState = 'calibrating'
        return {'success':'started calibration'}

    
    @inlineCallbacks
    def startCalibration(self, args):
        self.emitStateChanged()

        blackCal = args.get('blackCal') or args.get('fpn') or args.get('basic')
        zeroTimeBlackCal = args.get('zeroTimeBlackCal') or args.get('basic')
        analogCal = args.get('analogCal') or args.get('analog')

        if analogCal:
            logging.info('starting analog calibration')
            for delay in self.camera.sensor.startAnalogCal():
                yield asleep(delay)

        if zeroTimeBlackCal:
            logging.info('starting zero time black calibration')
            for delay in self.camera.startZeroTimeBlackCal():
                yield asleep(delay)
        elif blackCal:
            logging.info('starting standard black calibration')
            for delay in self.camera.startBlackCal():
                yield asleep(delay)
                
        self.currentState = 'idle'
        self.emitStateChanged(reason='calibration complete')


    #===============================================================================================
    #Method('getSequencerCapabilities', arguments='',      returns='a{sv}'),
    #Method('getSequencerProgram',      arguments='a{sv}', returns='a{sv}'),
    #Method('setSequencerProgram',      arguments='a{sv}', returns='a{sv}'),
    #Method('startRecord',              arguments='a{sv}', returns='a{sv}'),
    #Method('stopRecord',               arguments='a{sv}', returns='a{sv}'),
    
    @dbusMethod(krontechControl, 'getSequencerCapabilities')
    def dbusGetSequencerCapabilities(self):
        return {'failed':'Not Implemented Yet - poke otter', 'id':self.ERROR_NOT_IMPLEMENTED_YET}

    @dbusMethod(krontechControl, 'getSequencerProgram')
    def dbusGetSequencerProgram(self, args):
        return {'failed':'Not Implemented Yet - poke otter', 'id':self.ERROR_NOT_IMPLEMENTED_YET}

    @dbusMethod(krontechControl, 'setSequencerProgram')
    def dbusSetSequencerProgram(self, args):
        return {'failed':'Not Implemented Yet - poke otter', 'id':self.ERROR_NOT_IMPLEMENTED_YET}

    @dbusMethod(krontechControl, 'startRecord')
    def dbusStartRecord(self, args):
        return {'failed':'Not Implemented Yet - poke otter', 'id':self.ERROR_NOT_IMPLEMENTED_YET}

    @dbusMethod(krontechControl, 'stopRecord')
    def dbusStopRecord(self, args):
        return {'failed':'Not Implemented Yet - poke otter', 'id':self.ERROR_NOT_IMPLEMENTED_YET}










@inlineCallbacks
def main():
    cam = camera(lux1310())

    try:
        output = yield utils.getProcessOutput('python',['testFpga.py'], errortoo=True)
        if output and float(output) >= 3.19 and float(output) < 4.0:
            logging.info('FPGA State: All good. Current version: %0.2f', float(output))
        else:
            if output:
                logging.info('FPGA State: needs programming, version: %0.2f', float(output))
            else:
                logging.info('FPGA State: needs programming, unable to read from device')
            logging.info('======== resetting FPGA ==========')
            cam.reset(FPGA_BITSTREAM)
            
            output = yield utils.getProcessOutput('python',['testFpga.py'], errortoo=True)
            if output and float(output) >= 3.19 and float(output) < 4.0:
                logging.info('FPGA State: All good. Current version: %0.2f', float(output))
            else:
                logging.error('failed to bring up FPGA')
                reactor.stop()
    except Exception as e:
        logging.error(e)
        reactor.stop()
        
    try:
        conn = yield client.connect(reactor, 'system')
        conn.exportObject(controlApi(krontechControlPath, conn, cam) )
        yield conn.requestBusName(krontechControl)
        logging.info('Object exported on bus name "%s" with path "%s"', krontechControl, krontechControlPath)
    except error.DBusException as e:
        logging.error('Failed to export object: %s', e)
        reactor.stop()


if __name__ == "__main__":
    logging.basicConfig(level=logging.DEBUG, format='%(asctime)s %(levelname)s [%(funcName)s] %(message)s')
    reactor.callWhenRunning( main )
    reactor.run()


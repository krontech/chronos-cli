#!/usr/bin/python3
API_VERISON_STRING = '0.1'

from twisted.internet import reactor
from twisted.internet import defer
from twisted.internet import utils
from twisted.internet.defer import inlineCallbacks

from txdbus           import client, objects, error
from txdbus.interface import DBusInterface, Method, Signal
from txdbus.objects   import dbusMethod

import numpy
import logging

from camera import camera
from sensors import lux1310, frameGeometry
from regmaps import seqcommand
import regmaps
import pychronos
import json
import time

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
                          Method('setDescription',           arguments='a{sv}', returns='a{sv}'),
                          Method('status',                   arguments='',      returns='a{sv}'),
                          Signal('statusHasChanged',         arguments='a{sv}'),

                          Method('reinitSystem',             arguments='a{sv}', returns='a{sv}'),

                          Method('getSensorCapabilities',    arguments='',      returns='a{sv}'),
                          Method('getSensorSettings',        arguments='',      returns='a{sv}'),
                          Method('getSensorLimits',          arguments='a{sv}', returns='a{sv}'),
                          Method('setSensorSettings',        arguments='a{sv}', returns='a{sv}'),
                          Method('setSensorTiming',          arguments='a{sv}', returns='a{sv}'),

                          Method('getIoCapabilities',        arguments='',      returns='a{sv}'),
                          Method('getIoMapping',             arguments='',      returns='a{sv}'),
                          Method('setIoMapping',             arguments='a{sv}', returns='a{sv}'),
                          Method('getDelay',                 arguments='',      returns='a{sv}'),
                          Method('setDelay',                 arguments='a{sv}', returns='a{sv}'),
                          Signal('ioEvent',                  arguments='a{sv}'),
                          
                          Method('getCalCapabilities',       arguments='',      returns='a{sv}'),
                          Method('calibrate',                arguments='a{sv}', returns='a{sv}'),

                          Method('getColorMatrix',           arguments='',      returns='ad'),
                          Method('setColorMatrix',           arguments='ad',    returns='ad'),
                          Method('getWhiteBalance',          arguments='',      returns='a{sv}'),
                          Method('setWhiteBalance',          arguments='a{sv}', returns='a{sv}'),

                          Method('getSequencerCapabilities', arguments='',      returns='a{sv}'),
                          Method('getSequencerProgram',      arguments='a{sv}', returns='a{sv}'),
                          Method('setSequencerProgram',      arguments='a{sv}', returns='a{sv}'),
                          Method('startRecord',              arguments='a{sv}', returns='a{sv}'),
                          Method('stopRecord',               arguments='a{sv}', returns='a{sv}'),

                          Method('forceCancel',              arguments='',      returns='')
    )
    dbusInterfaces = [iface]

    ## This feels like a duplication of the Python exceptions.
    ERROR_NOT_IMPLEMENTED_YET = 9999
    VALUE_ERROR               = 1

    def __init__(self, objectPath, conn, camera):
        self.conn = conn
        super().__init__(objectPath)
        self.forceCancel = False

        # hack to get focus peaking working
        #self.pychronos

        self.camera = camera
        self.io = regmaps.ioInterface()
        self.display = regmaps.display()
        self.description = "Chronos SN:%s" % (self.camera.getSerialNumber())

        self.config = {}
        self.lastSaveTime = None
        
        self.currentState = 'initializing'

        reactor.callLater(0.05, self.reinitSystem, {'reset':True, 'sensor':True})
        reactor.callLater(6.00, self.loadConfig)

    def loadConfig(self, configFile='/var/camera/apiConfig.json'):
        try:
            logging.info('checking for config file: %s', configFile)
            with open(configFile, 'r') as inFile:
                self.config = json.load(inFile)
            logging.info('loaded config file')
            if self.config['sensorSettings'].get('hOffset',0) <   0: self.config['sensorSettings']['hOffset'] = 0
            if self.config['sensorSettings'].get('vOffset',0) <   0: self.config['sensorSettings']['vOffset'] = 0
            if self.config['sensorSettings'].get('hRes',1280) < 320: self.config['sensorSettings']['hRes'] = 1280
            if self.config['sensorSettings'].get('vRes',1024) <  96: self.config['sensorSettings']['vRes'] = 1024

            # reset because I can't make this work
            self.config['sensorSettings'] = {
                'hRes':1280,
                'vRes':1024,
                'hOffset':0,
                'vOffset':0,
                'vDarkRows':0,
                'framePeriod':1/1041.67,
                'program': 'standard',
                'exposure': 0.000020,
                'zebra': True,
                'peaking': True,
                'nTriggerFrames': 5
            }
            reactor.callLater(0.01, self.setSensorSettings, self.config['sensorSettings'])
            self.description = self.config['description']
            self.idNumber    = self.config['idNumber']
            
            ## disabled until I can test it further
            #self.io.setConfiguration(self.config['ioMapping'])

            # reset as there's no way to set it yet and I don't
            # want corrupted data anywhere
            self.config['colorMatrix'] = [ 1.9147, -0.5768, -0.2342,
                                           -0.3056, 1.3895, -0.0969,
                                           0.1272, -0.9531, 1.6492 ]
            self.dbusSetColorMatrix(self.config['colorMatrix'])
            self.dbusSetWhiteBalance(self.config['whiteBalance'])
            
        except FileNotFoundError:
            logging.info('config file not found')
            self.setConfigToDefaults(configFile)
        except ValueError as e:
            logging.info('value error found in config file: %s', e) 
            self.setConfigToDefaults(configFile)

    def setConfigToDefaults(self, configFile='/var/camera/apiConfig.json'):
        logging.info('setting config to defaults')
        self.config['sensorSettings'] = {
            'hRes':1280,
            'vRes':1024,
            'framePeriod':1/1041.67,
            'program': 'standard',
            'exposure': 0.000020,
            'zebra': True,
            'peaking': True,
            'nTriggerFrames': 5
        }
        
        self.config['description']  = self.description
        self.config['idNumber']     = self.idNumber
        
        self.config['ioMapping']    = self.io.getConfiguration()
        
        self.config['colorMatrix'] = [ 1.9147, -0.5768, -0.2342,
                                       -0.3056, 1.3895, -0.0969,
                                       0.1272, -0.9531, 1.6492 ]
        self.config['whiteBalance'] = {'whitebalance': {'red':1.5226, 'green':1.0723, 'blue':1.5655}}
        self.saveConfig(configFile)
        # immediately load so it's applied
        self.loadConfig(configFile)

    def saveConfig(self, configFile='/var/camera/apiConfig.json', force=False):
        if not force and self.lastSaveTime:
            if time.time() - self.lastSaveTime < 5.0:
                # don't save every second - only every five seconds
                logging.info('skipping save as it\'s too soon after a previous one')
                return
        logging.info('saving config file: %s', configFile)
        self.lastSaveTime = time.time()
        with open(configFile, 'w') as outFile:
            json.dump(self.config, outFile, sort_keys=True, indent=4, separators=(',', ': '))
            
    # Return a state dictionary
    def status(self, lastState=None, error=None):
        data = {'state':self.currentState}
        if lastState:
            data['lastState'] = lastState
        if error:
            data['error'] = error
        return data

    @inlineCallbacks
    def pokeCamPipelineToRestart(self, geometry=None, zebra=True, peaking=False):
        logging.info('Notifying cam-pipeline to reconfigure display')
        if not geometry:
            geometry = self.camera.geometry
        #utils.getProcessOutput('killall', ['-HUP', 'cam-pipeline'])
        videoApi = yield self.conn.getRemoteObject('com.krontech.chronos.video',   '/com/krontech/chronos/video')
        logging.debug('have videoApi')
        settings = {
            'hres':geometry.hRes,
            'vres':geometry.vRes,
            'peaking':peaking,
            'zebra':zebra}
        logging.debug('sending to livedisplay: %s', settings)
        reply = yield videoApi.callRemote('livedisplay', settings)
        logging.debug('sent')

    @property
    def idNumber(self):
        try:
            idFile = open("/var/camera/idNum.txt", "r")
            logging.debug('opened file')
            idNum = int(idFile.read())
            logging.debug('read ID')
            idFile.close()
            logging.debug('ID: %d', idNum)
            return idNum
        except Exception as e:
            logging.debug('failed to load ID: %s', e)
            return None
    @idNumber.setter
    def idNumber(self, value):
        idFile = open("/var/camera/idNum.txt", "w+")
        idFile.write(str(value))
        idFile.close()
        hostnameFile = open("/etc/hostname", "w")
        hostnameFile.write('Chronos-%02d\n' % value)
        hostnameFile.close()

    #===============================================================================================
    #Method('status',                   arguments='',      returns='a{sv}'),
    #Signal('statusHasChanged',         arguments=''),

    @dbusMethod(krontechControl, 'status')
    def dbusStatus(self):
        self.emitStateChanged(reason='polled')
        return self.status()

    def emitStateChanged(self, reason=None, details=None):
        data = {'state':self.currentState}
        if reason:
            data['reason'] = reason
        if details:
            data['details'] = details
        self.emitSignal('statusHasChanged', (data))
        
    
    #===============================================================================================
    #Method('getCameraData',            arguments='',      returns='a{sv}'),
    #Method('getSensorData',            arguments='',      returns='a{sv}'),
    
    @dbusMethod(krontechControl, 'getCameraData')
    def dbusGetCameraData(self):
        data = {
            'model':'Chronos1.4',
            'apiVersion':API_VERISON_STRING,
            'serial':self.camera.getSerialNumber().strip(),
            'description':self.description,
            'delay':self.io.delayTime
        }
        if (self.idNumber is not None):
            data['idNumber'] = self.idNumber
        return data

    @dbusMethod(krontechControl, 'getSensorData')
    def dbusGetSensorData(self):
        geometry = self.camera.sensor.getMaxGeometry()
        cfaPattern = self.camera.sensor.cfaPattern
        data = {
            'name': self.camera.sensor.name,
            'pixelRate':1.4*10**9,
            'hMax': geometry.hRes,
            'vMax': geometry.vRes,
            'hMin': 320,
            'vMin': 2,
            'hIncrement': 16,
            'vIncrement': 2,
            'pixelFormat': 'Y12'
        }
        # If a color sensor, set the pixel format accordingly.
        if (cfaPattern):
            cfaString = "BYR2-"
            for color in cfaPattern:
                cfaString += color
            data["pixelFormat"] = cfaString
        
        return data

    @dbusMethod(krontechControl, 'setDescription')
    def dbusSetDescription(self, args):
        if args["description"]:
            self.description = str(args["description"])
        if args["idNumber"] is not None:
            self.idNumber = int(args["idNumber"])
        return self.status()

    
    #===============================================================================================
    #Method('reinitSystem',             arguments='a{sv}', returns='a{sv}'),
    
    @dbusMethod(krontechControl, 'reinitSystem')
    def dbusReinitSystem(self, args):
        reactor.callLater(0.0, self.reinitSystem, args)
        self.currentState = 'reinitializing'
        return self.status()

    def reinitSystem(self, args):
        recal = False
        self.emitStateChanged()
        reinitAll = args.get('all', False)
        if args.get('fpga') or reinitAll:
            reinitAll = True
            recal = True
            self.camera.reset(FPGA_BITSTREAM)

        if args.get('reset'):
            #recal = True
            self.camera.reset()
            
        if args.get('sensor') or reinitAll:
            recal = True
            self.camera.sensor.reset()

        if recal:
            self.currentState = 'calibrating'
            settings = self.config.get('sensorSettings')
            if settings:
                reactor.callLater(0.05, self.loadConfig)
        else:
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
        logging.info('setSensorSettings args: %s', args)
        geom = self.camera.sensor.getCurrentGeometry()
        logging.info('getCurrentGeometry results: %s', geom)
        geom.hRes      = args.get('hRes',      geom.hRes)
        geom.vRes      = args.get('vRes',      geom.vRes)
        geom.hOffset   = args.get('hOffset',   geom.hOffset)
        geom.vOffset   = args.get('vOffset',   geom.vOffset)
        geom.vDarkRows = args.get('vDarkRows', geom.vDarkRows)
        geom.bitDepth  = args.get('bitDepth',  geom.bitDepth)
        zebra = args.get('zebra', True)
        peaking = args.get('peaking', True)

        logging.info('after mixing: %s', geom)

        programName = args.get('program', 'standard')
        program = 0
        if   programName == 'standard':
            program = self.camera.sensor.PROGRAM_STANDARD
            self.io.shutterTriggersFrame = False
        elif programName == 'shutterGating':
            program = self.camera.sensor.PROGRAM_SHUTTER_GATING
            self.io.shutterTriggersFrame = True

        # set default value
        logging.info("geom: %s", geom)
        framePeriod, _ = self.camera.sensor.getPeriodRange(geom)

        # check if we have a frameRate field and if so convert it to framePeriod
        frameRate = args.get('frameRate')
        if frameRate:
            framePeriod = 1.0 / frameRate

        # if we have a framePeriod explicit field, override frameRate or default value
        framePeriod = args.get('framePeriod', framePeriod)

        # set exposure or use a default of 95% framePeriod
        exposurePeriod = args.get('exposure', framePeriod * 0.95)

        # nFrames if the trigger nframes mode is used
        nFrames = args.get('nTriggerFrames', 5)
        
        # set up video
        self.camera.setRecordingConfig(geom, framePeriod, exposurePeriod, program=program, nFrames=nFrames)

        # tell video pipeline to restart
        logging.debug('about to schedule pokeCamPipeline')
        reactor.callLater(0.01, self.pokeCamPipelineToRestart, geom, zebra, peaking)

        # start a calibration loop
        reactor.callLater(0.05, self.startCalibration, {'analog':True, 'zeroTimeBlackCal':True})

        # get the current config so we can return the real values
        appliedGeometry = self.dbusGetSensorSettings()
        appliedGeometry['zebra'] = zebra
        appliedGeometry['peaking'] = peaking
        appliedGeometry['nTriggerFrames'] = nFrames

        self.currentState = 'calibrating'
        reactor.callLater(0.0, self.emitStateChanged, reason='resolution changed', details={'geometry':appliedGeometry})

        self.config['sensorSettings'] = appliedGeometry
        reactor.callLater(0.0, self.saveConfig)

        return appliedGeometry

    @dbusMethod(krontechControl, 'setSensorTiming')
    def setSensorTiming(self, args):
        programName = args.get('program', 'standard')
        program = self.camera.sensor.PROGRAM_STANDARD

        geom = self.camera.sensor.getCurrentGeometry()

        if programName == 'shutterGating':
            program = self.camera.sensor.PROGRAM_SHUTTER_GATING
            self.io.shutterTriggersFrame = True

        if program == self.camera.sensor.PROGRAM_SHUTTER_GATING:
            self.camera.sensor.setResolution(geom, program=program)
            return {"program":"shutterGating"}

        # check if we have a frameRate field and if so convert it to framePeriod
        frameRate = args.get('frameRate')
        if frameRate:
            framePeriod = 1.0 / frameRate

        # if we have a framePeriod explicit field, override frameRate or default value
        framePeriod = args.get('framePeriod', framePeriod)

        # set exposure or use a default of 95% framePeriod
        exposurePeriod = args.get('exposure', framePeriod * 0.95) # self.camera.sensor.getExposureRange(geom))

        self.camera.sensor.setResolution(geom, fPeriod=framePeriod,
                                         exposure=exposurePeriod,
                                         program=program)
        self.io.shutterTriggersFrame = False
        #self.camera.sensor.setFramePeriod(framePeriod)
        #self.camera.sensor.setExposurePeriod(exposurePeriod)
        self.config['sensorSettings']['framePeriod'] = framePeriod
        self.config['sensorSettings']['exposure'] = exposurePeriod
        reactor.callLater(0.0, self.saveConfig)
        
        returnData = dict()
        returnData['framePeriod'] = self.camera.sensor.getCurrentPeriod()
        returnData['frameRate']   = 1.0 / returnData['framePeriod']
        returnData['exposure']    = self.camera.sensor.getCurrentExposure()
        return returnData
        
    #===============================================================================================
    #Method('getIoCapabilities',        arguments='',      returns='a{sv}'),
    #Method('getIoMapping',             arguments='a{sv}', returns='a{sv}'),
    #Method('setIoMapping',             arguments='a{sv}', returns='a{sv}'),
    #Signal('ioEvent',                  arguments='a{sv}'),

    @dbusMethod(krontechControl, 'getIoCapabilities')
    def dbusGetIoCapabilities(self):
        return {'failed':'Not Implemented Yet - poke otter', 'id':self.ERROR_NOT_IMPLEMENTED_YET}

    @dbusMethod(krontechControl, 'getIoMapping')
    def dbusGetIoMapping(self):
        mapping = self.io.getConfiguration()
        logging.info('mapping: %s', mapping)
        return mapping

    @dbusMethod(krontechControl, 'setIoMapping')
    def dbusSetIoMapping(self, args):
        logging.info('mapping given: %s', args)
        self.io.setConfiguration(args)
        self.config['ioMapping'] = self.io.getConfiguration()
        reactor.callLater(0.0, self.saveConfig)
        return self.config['ioMapping']

    @dbusMethod(krontechControl, 'getDelay')
    def dbusGetDelay(self, args):
        return self.io.getDelayConfiguraiton()
    
    @dbusMethod(krontechControl, 'setDelay')
    def dbusSetDelay(self, args):
        logging.info('delay given: %s', args)
        self.io.setDelayConfiguration(args)
        self.config['ioMapping'] = self.io.getConfiguration()
        reactor.callLater(0.0, self.saveConfig)
        return self.io.getDelayConfiguration()

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
        return self.status()

    @inlineCallbacks
    def startCalibration(self, args):
        self.emitStateChanged()

        blackCal         = args.get('blackCal') or args.get('fpn')
        zeroTimeBlackCal = args.get('zeroTimeBlackCal') or args.get('basic')
        analogCal        = args.get('analogCal') or args.get('analog') or args.get('basic')
        whiteBalance     = args.get('whiteBalance')

        if analogCal:
            logging.info('starting analog calibration')
            for delay in self.camera.sensor.startAnalogCal():
                if self.forceCancel: break
                yield asleep(delay)

        if zeroTimeBlackCal:
            logging.info('starting zero time black calibration')
            for delay in self.camera.startZeroTimeBlackCal():
                if self.forceCancel: break
                yield asleep(delay)
        elif blackCal:
            logging.info('starting standard black calibration')
            for delay in self.camera.startBlackCal():
                if self.forceCancel: break
                yield asleep(delay)

        if whiteBalance:
            logging.info('starting white balance')
            for delay in self.camera.startWhiteBalance():
                if self.forceCancel: break
                yield asleep(delay)
            self.config['whiteBalance'] = self.dbusGetWhiteBalance()
            self.saveConfig(force=True)
                
        self.currentState = 'idle'
        self.emitStateChanged(reason='calibration complete')

    #===============================================================================================
    #Method('getColorMatrix',           arguments='',      returns='a{sv}'),
    #Method('setColorMatrix',           arguments='a{sv}', returns='a{sv}'),
    #Method('getWhiteBalance',          arguments='',      returns='a{sv}'),
    #Method('setWhiteBalance',          arguments='a{sv}', returns='a{sv}'),
    
    @dbusMethod(krontechControl, 'getColorMatrix')
    def dbusGetColorMatrix(self):
        colorMatrix = [self.display.colorMatrix[0]/4096.0,self.display.colorMatrix[1]/4096.0,self.display.colorMatrix[2]/4096.0,
                       self.display.colorMatrix[3]/4096.0,self.display.colorMatrix[4]/4096.0,self.display.colorMatrix[5]/4096.0,
                       self.display.colorMatrix[6]/4096.0,self.display.colorMatrix[7]/4096.0,self.display.colorMatrix[8]/4096.0]
        logging.info('colorMatrix: %s', str(colorMatrix))
        return colorMatrix
    
    @dbusMethod(krontechControl, 'setColorMatrix')
    def dbusSetColorMatrix(self, args):
        if type(args) == list:
            if len(args) == 9:
                for i in range(9):
                    self.display.colorMatrix[i] = int(args[i] * 4096)
            else:
                logging.error("------- length of arguments too short")
        else:
            logging.error("------ type for colormatrix wrong(%s)", type(args))
        self.config['colorMatrix'] = self.dbusGetColorMatrix()
        reactor.callLater(0.0, self.saveConfig)
        return self.config['colorMatrix']

    @dbusMethod(krontechControl, 'getWhiteBalance')
    def dbusGetWhiteBalance(self):
        red   = self.display.whiteBalance[0] / 4096.0
        green = self.display.whiteBalance[1] / 4096.0
        blue  = self.display.whiteBalance[2] / 4096.0
        whiteBalance = {'red':red, 'green':green, 'blue':blue}
        logging.info('whiteBalance: %s (type:%s)', str(whiteBalance), str(type(whiteBalance)))
        return whiteBalance

    @dbusMethod(krontechControl, 'setWhiteBalance')
    def dbusSetWhiteBalance(self, args):
        red   = args.get('red')
        green = args.get('green')
        blue  = args.get('blue')
        if not red or not green or not blue:
            return {'failed':'args does not contain all of red, green and blue values', 'id':self.VALUE_ERROR}
        
        self.display.whiteBalance[0] = int(red   * 4096)
        self.display.whiteBalance[1] = int(green * 4096)
        self.display.whiteBalance[2] = int(blue  * 4096)
        self.config['whiteBalance'] = self.dbusGetWhiteBalance()
        reactor.callLater(0.0, self.saveConfig)
        return self.config['whiteBalance']

    

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
        if self.currentState != 'idle':
            return {'failed':'busy', 'state':self.currentState}
        
        self.currentState = 'recording'
        reactor.callLater(0.0, self.startRecord, args)
        
        return {'success':'started recording'}

        
    @dbusMethod(krontechControl, 'stopRecord')
    def dbusStopRecord(self, args):
        self.camera.stopRecording()
        return {'success':'stopped recording'}
    
    @inlineCallbacks
    def startRecord(self, args):
        self.emitStateChanged()

        # send flush
        
        geometry = self.camera.sensor.getCurrentGeometry()

        # set up some defaults
        if not 'blkTermFull'     in args: args['blkTermFull']     = True
        if not 'recTermMemory'   in args: args['recTermMemory']   = True
        if not 'recTermBlockEnd' in args: args['recTermBlockEnd'] = True

        # use blockSize if given; otherwise use nFrames or use getRecordMaxFrames
        nFrames = args.get('nFrames', self.camera.getRecordingMaxFrames(geometry))
        args['blockSize'] = args.get('blockSize', nFrames)

        # override a few variables
        args['nextState'] = 0
        
        # TODO: make this use args
        program = [ seqcommand(**args) ]

        logging.info('Recording program: %s', program[0])
        
        for delay in self.camera.startRecording(program):
            if self.forceCancel: break
            yield asleep(delay)

        self.camera.stopRecording()
        
        self.currentState = 'idle'
        self.emitStateChanged(reason='recording complete')

    
    #===============================================================================================
    #Method('forceCancel',              arguments='',      returns='')
    @dbusMethod(krontechControl, 'forceCancel')
    def dbusForceCancel(self):
        reactor.callLater(0.0, self.runForceCancel)

    @inlineCallbacks
    def runForceCancel(self):
        logging.info("starting forced cancel")
        self.forceCancel = True
        self.camera.forceCancel = True
        self.camera.sensor.forceCancel = True
        yield asleep(0.5)
        self.forceCancel = False
        self.camera.forceCancel = False
        self.camera.sensor.forceCancel = False
        logging.info("finished forced cancel")
        









@inlineCallbacks
def main():
    cam = camera(lux1310())

    try:
        conn = yield client.connect(reactor, 'system')
        conn.exportObject(controlApi(krontechControlPath, conn, cam) )
        yield conn.requestBusName(krontechControl)
        logging.info('Object exported on bus name "%s" with path "%s"', krontechControl, krontechControlPath)
    except error.DBusException as e:
        logging.error('Failed to export object: %s', e)
        reactor.stop()


if __name__ == "__main__":
    logging.basicConfig(level=logging.DEBUG, format='%(levelname)s [%(funcName)s] %(message)s')
    reactor.callWhenRunning( main )
    reactor.run()


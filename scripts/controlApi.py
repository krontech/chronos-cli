#!/usr/bin/python
API_VERISON_STRING = '0.1'

import numpy

from twisted.internet import reactor, defer
from twisted.internet.defer import inlineCallbacks

from txdbus           import client, objects, error
from txdbus.interface import DBusInterface, Method, Signal
from txdbus.objects   import dbusMethod

import logging

import camera
import lux1310

krontechControl     =  'com.krontech.chronos.control'
krontechControlPath = '/com/krontech/chronos/control'



class controlApi(objects.DBusObject):
    iface = DBusInterface(krontechControl,
                          Method('getCameraData',            arguments='',      returns='a{sv}'),
                          Method('getSensorData',            arguments='',      returns='a{sv}'),
                          Method('status',                   arguments='',      returns='a{sv}'),
                          Signal('statusHasChanged',         arguments=''),

                          Method('reinitSystem',             arguments='a{sv}', returns='a{sv}'),

                          Method('getSensorCapabilities',    arguments='',      returns='a{sv}'),
                          Method('getSensorSettings',        arguments='',      returns='a{sv}'),
                          Method('getSensorLimits',          arguments='a{sv}', returns='a{sv}'),
                          Method('setSensorSettings',        arguments='a{sv}', returns='a{sv}'),

                          Method('getIoCapabilities',        arguments='',      returns='a{sv}'),
                          Method('getIoMapping',             arguments='a{sv}', returns='a{sv}'),
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
    
    
    def __init__(self, objectPath, conn):
        self.conn = conn
        super().__init__(objectPath)
        self.currentState = 'idle'
        
        self.camera = camera.camera(lux1310.lux1310())


    def emitStateChanged(self, reason=None):
        pass
        
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

    @inlineCallbacks
    def reinitSystem(self, args):
        emitStateChanged()
        reinitAll = args.get('all', False)
        if args.get('fpga') or reinitAll:
            reinitAll = True
            self.camera = camera.camera(lux1310.lux1310())

        if args.get('sensor') or reinitAll:
            self.camera.sensor.boot()

        self.currentState = 'idle'
        emitStateChanged()

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
        return {'failed':'Not Implemented Yet - poke otter', 'id':self.ERROR_NOT_IMPLEMENTED_YET}

    @dbusMethod(krontechControl, 'getSensorLimits')
    def dbusGetSensorLimits(self, args):
        return {'failed':'Not Implemented Yet - poke otter', 'id':self.ERROR_NOT_IMPLEMENTED_YET}

    @dbusMethod(krontechControl, 'setSensorSettings')
    def setSensorSettings(self, args):
        return {'failed':'Not Implemented Yet - poke otter', 'id':self.ERROR_NOT_IMPLEMENTED_YET}

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
        return {'failed':'Not Implemented Yet - poke otter', 'id':self.ERROR_NOT_IMPLEMENTED_YET}

    @dbusMethod(krontechControl, 'setIoMapping')
    def dbusSetIoMapping(self, args):
        return {'failed':'Not Implemented Yet - poke otter', 'id':self.ERROR_NOT_IMPLEMENTED_YET}

    #===============================================================================================
    #Method('getCalCapabilities',       arguments='',      returns='a{sv}'),
    #Method('calibrate',                arguments='a{sv}', returns='a{sv}'),
    
    @dbusMethod(krontechControl, 'getCalCapabilities')
    def dbusGetCalCapabilities(self):
        return {'failed':'Not Implemented Yet - poke otter', 'id':self.ERROR_NOT_IMPLEMENTED_YET}

    @dbusMethod(krontechControl, 'calibrate')
    def dbusCalibrate(self, args):
        return {'failed':'Not Implemented Yet - poke otter', 'id':self.ERROR_NOT_IMPLEMENTED_YET}


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
    try:
        conn = yield client.connect(reactor, 'system')
        conn.exportObject(controlApi(krontechControlPath, conn) )
        yield conn.requestBusName(krontechControl)
        print('Object exported on bus name "%s" with path "%s"' % (krontechControl, krontechControlPath))
    except error.DBusException as e:
        print('Failed to export object: ', e)
        reactor.stop()


if __name__ == "__main__":
    logging.basicConfig(level=logging.DEBUG, format='%(asctime)s %(levelname)s [%(funcName)s] %(message)s')
    reactor.callWhenRunning( main )
    reactor.run()


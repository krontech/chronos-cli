#!/usr/bin/python
import numpy

from twisted.internet import reactor, defer

from txdbus           import client, objects, error
from txdbus.interface import DBusInterface, Method, Signal
from txdbus.objects   import dbusMethod

import logging


krontechRing     =  'com.krontech.chronos'
krontechRingPath = '/ring'



class controlApi(objects.DBusObject):
    iface = DBusInterface(krontechRing,
                          Method('timing_master',     arguments='a{sv}', returns='a{sv}'),

                          Method('aim_cameras',       arguments='a{sv}', returns='a{sv}'),
                          Method('cal_fpn',           arguments='a{sv}', returns='a{sv}'),
                          Method('tap_in',            arguments='a{sv}', returns='a{sv}'),
                          Method('set_frame_delay',   arguments='a{sv}', returns='a{sv}'),

                          Method('record',            arguments='a{sv}', returns='a{sv}'),
                          Signal('record_finished',   arguments='a{sv}'),

                          Method('save_footage',      arguments='a{sv}', returns='a{sv}'),
                          Signal('save_progress',     arguments='a{sv}'),
                          Signal('save_finished',     arguments='a{sv}'),
                          
                          Method('test',              arguments='',      returns='a{sv}'),
    )

    dbusInterfaces = [iface]

    def __init__(self, objectPath, conn):
        self.conn = conn
        super().__init__(objectPath)
        self.currentState = 'idle'

        self.aimCamera = aimCamera.aimCamera()
        self.io        = ioInterface.ioInterface()
        self.timing    = timingGenerator.sensorTiming()








@inlineCallbacks
def main():
    try:
        conn = yield client.connect(reactor, 'system')
        conn.exportObject(chronosControl(krontechRingPath, conn) )
        yield conn.requestBusName(krontechRing)
        print('Object exported on bus name "%s" with path "%s"' % (krontechRing, krontechRingPath))
    except error.DBusException as e:
        print('Failed to export object: ', e)
        reactor.stop()


if __name__ == "__main__":
    logging.basicConfig(level=logging.DEBUG, format='%(asctime)s %(levelname)s [%(funcName)s] %(message)s')
    reactor.callWhenRunning( main )
    reactor.run()


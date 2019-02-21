import sys

from twisted.web import server, resource
from twisted.web.static import File
from twisted.internet import reactor, defer, utils
from twisted.python import log
from twisted.internet.defer import inlineCallbacks

from txdbus import client, error

import json
import logging

import cgi

def asleep(secs):
    """
    @brief Do a reactor-safe sleep call. Call with yield to block until done.
    @param secs Time, in seconds
    @retval Deferred whose callback will fire after time has expired
    """
    d = defer.Deferred()
    reactor.callLater(secs, d.callback, None)
    return d


eventList = {'test':'',
             'control/statusHasChanged':'/com/krontech/chronos/control/statusHasChanged',
             'control/ioEvent':'/com/krontech/chronos/control/ioEvent',
             'video/segment':'/com/krontech/chronos/video/segment',
             'video/eof':'/com/krontech/chronos/video/eof',
             'video/sof':'/com/krontech/chronos/video/sof'}


class Root(resource.Resource):
    """
    Root resource; serves JavaScript
    """
    def getChild(self, name, request):
        if name == '':
            return self
        return resource.Resource.getChild(self, name, request)

    def render_GET(self, request):
        returnString = """
        <html>
            <head>
                <script language="JavaScript">
                        function updateEventDetails(event) {
                            eventName = document.getElementById("event-name");
                            eventData = document.getElementById("event-data");
                            eventName.innerHTML = event.type;
                            eventData.innerHTML = event.data;
                        }

                        eventSource = new EventSource("/subscribe");
        """
        for name in eventList.keys():
            returnString += '\n                        eventSource.addEventListener("{name}", updateEventDetails, false);'.format(name=name)
        returnString += """
                        eventSource.addEventListener("test", updateEventDetails, false);
                        eventSource.onmessage = updateEventDetails;
                    </script>
            </head>
            <body>
                <h3> Event name: </h3>
                <p id="event-name"></p>
                <h3> Event data: </h3>
                <p id="event-data"></p>
            </body>
        </html>
        """
        return bytes(returnString, 'utf8')


class Method(resource.Resource):
    """
    Implements a callable method with arguments on dbus
    """
    isLeaf = True
    def __init__(self, parent, bus, methodName, arguments=False):
        self.parent = parent
        self.bus = bus
        self.methodName = methodName
        self.arguments = arguments

        self.parent.putChild(bytes(methodName, 'utf8'), self)

    def render_GET(self, request):
        if self.arguments:
            return b'"data" field required using POST'
        reactor.callLater(0.0, self.startDbusRequest, request)
        return server.NOT_DONE_YET
        
    def render_POST(self, request):
        if self.arguments:
            rawData = request.args.get(b'data', None)
            if not rawData:
                request.setResponseCode(400)
                return b'"data" field required'
            data = ''
            for line in rawData:
                data += line.decode('utf8')
            data = json.loads(data)

            reactor.callLater(0.0, self.startDbusRequestWData, request, data)
            return server.NOT_DONE_YET
        else:
            reactor.callLater(0.0, self.startDbusRequest, request)
            return server.NOT_DONE_YET

    @inlineCallbacks
    def startDbusRequestWData(self, request, data):
        reply = yield self.bus.callRemote(self.methodName, data)

        returnData = json.dumps(reply)
        
        request.write(bytes('{0}\n'.format(returnData), 'utf8'))
        request.finish()

        logging.info('method "%s" requested with data: %s with response: %s', self.methodName, data, returnData)

    @inlineCallbacks
    def startDbusRequest(self, request):
        reply = yield self.bus.callRemote(self.methodName)

        returnData = json.dumps(reply)
        
        request.write(bytes('{0}\n'.format(returnData), 'utf8'))
        request.finish()

        logging.info('method "%s" requested with response: %s', self.methodName, returnData)
        
class Subscribe(resource.Resource):
    """
    Implements the subscribe resource
    """
    isLeaf = True

    def __init__(self):
        self.subscribers = set()

    def render_GET(self, request):
        request.setHeader('Content-Type', 'text/event-stream; charset=utf-8')
        request.setResponseCode(200)
        self.subscribers.add(request)
        d = request.notifyFinish()
        d.addBoth(self.removeSubscriber)
        log.msg("Adding subscriber...")
        request.write(b"")
        return server.NOT_DONE_YET

    def publishToAll(self, event, data):
        """
        Publish an event to all recipients.
        """
        for subscriber in self.subscribers:
            if event:
                subscriber.write(bytes('event: {event}\n'.format(event=event), 'utf8'))
            subscriber.write(bytes('data: {data}\n'.format(data=data), 'utf8'))
            # NOTE: the last CRLF is required to dispatch the event at the client
            subscriber.write(b"\n")

    def removeSubscriber(self, subscriber):
        if subscriber in self.subscribers:
            log.msg("Removing subscriber..")
            self.subscribers.remove(subscriber)


class Publish(resource.Resource):
    """
    Implements the publish resource
    """
    isLeaf = True

    def __init__(self, subscriber):
        self.subscriber = subscriber

    def render_POST(self, request):
        eventlines = request.args.get(b'event', [b''])
        event = ''
        for line in eventlines:
            event += line.decode('utf8')
        rawData  = request.args.get(b'data', None)
        if not rawData:
            request.setResponseCode(400)
            return b"The parameter 'data' must be set\n"
        data = ''
        for line in rawData:
            logging.info('line length: %d', len(line))
            logging.info(' utf8 length: %d', len(line.decode('utf8')))
            data += line.decode('utf8')
        self.subscriber.publishToAll(event, data)
        
        return bytes('event: {event}\ndata: {data}\n'.format(event=event, data=data), 'utf8')


class dbusPublisher:
    def __init__(self, subscriber, controlApi, videoApi, ringApi):
        self.subscriber = subscriber
        self.controlApi = controlApi
        self.videoApi = videoApi
        self.ringApi = ringApi

        self.controlApi.notifyOnSignal('statusHasChanged', self.publishStatusChanged)
        self.videoApi.notifyOnSignal('segment', self.publishSegment)
        self.videoApi.notifyOnSignal('sof', self.publishSOF)
        self.videoApi.notifyOnSignal('eof', self.publishEOF)

    def publishStatusChanged(self, signal):
        event = 'control/statusHasChanged'
        data = json.dumps(signal)
        logging.info('%s: %s', event, data)
        self.subscriber.publishToAll(event, data)

    def publishSegment(self, signal):
        event = 'video/segment'
        data = json.dumps(signal)
        logging.info('%s: %s', event, data)
        self.subscriber.publishToAll(event, data)

    def publishSOF(self, signal):
        event = 'video/segment'
        data = json.dumps(signal)
        logging.info('%s: %s', event, data)
        self.subscriber.publishToAll(event, data)

    def publishEOF(self, signal):
        event = 'video/segment'
        data = json.dumps(signal)
        logging.info('%s: %s', event, data)
        self.subscriber.publishToAll(event, data)


class previewImage(resource.Resource):
    isLeaf = True
    def __init__(self, bus):
        self.bus = bus
        super().__init__()

    def render_GET(self, request):
        request.setHeader('Content-Type', 'image/jpeg')
        reactor.callLater(0.0, self.requestPlaybackImage, request)
        return server.NOT_DONE_YET

    @inlineCallbacks
    def requestPlaybackImage(self, request):
        reply = yield self.bus.callRemote('livedisplay')
        yield asleep(1/60)

        image = open('/tmp/cam-screencap.jpg', 'br').read()
        request.write(image)
        request.finish()

class playbackImage(resource.Resource):
    isLeaf = True
    def __init__(self, bus):
        self.bus = bus
        super().__init__()
    
    def render_GET(self, request):
        request.setHeader('Content-Type', 'image/jpeg')
        reactor.callLater(0.0, self.requestPlaybackImage, request)
        return server.NOT_DONE_YET

    @inlineCallbacks
    def requestPlaybackImage(self, request):
        frameNum = int(request.args.get(b'frameNum', (0))[0])
        reply = yield self.bus.callRemote('playback', {"framerate":0, "position":frameNum})
        yield asleep(1/60)

        image = open('/tmp/cam-screencap.jpg', 'br').read()
        request.write(image)
        request.finish()


class waitForTouch(resource.Resource):
    isLeaf = True
    def render_GET(self, request):
        reactor.callLater(0.0, self.runCommand, request)
        return server.NOT_DONE_YET

    @inlineCallbacks
    def runCommand(self, request):
        message = cgi.escape(request.args.get(b'message', (b"Tap"))[0].decode('utf8'))
        logging.info('started wait for touch')
        yield utils.getProcessOutput('python3', ['uiScripts/tap-to-exit-button.py', message], env={"QT_QPA_PLATFORM":"linuxfb:fb=/dev/fb0"})
        logging.info('touch happened')
        request.write(b'{"Touched":true}')
        request.finish()

class waitForTouchThenBlackcal(resource.Resource):
    isLeaf = True
    def __init__(self, controlApi):
        self.controlApi = controlApi
        super().__init__()
    
    def render_GET(self, request):
        reactor.callLater(0.0, self.runCommand, request)
        return server.NOT_DONE_YET
        
    @inlineCallbacks
    def runCommand(self, request):
        message = cgi.escape(request.args.get(b'message', (b"Touch to cal"))[0].decode('utf8'))
        logging.info('started wait for touch')
        yield utils.getProcessOutput('python3', ['uiScripts/tap-to-exit-button.py', message], env={"QT_QPA_PLATFORM":"linuxfb:fb=/dev/fb0"})
        logging.info('touch happened; calibrating')

        reply = yield self.controlApi.callRemote('calibrate', {"blackCal":True, "analogCal":True})
        returnData = json.dumps(reply)
        request.write(bytes('{0}\n'.format(returnData), 'utf8'))
        request.finish()
        
@inlineCallbacks
def main():
    subscribe = Subscribe()
    webPublisher = Publish(subscribe)
    root.putChild(b'subscribe', subscribe)
    root.putChild(b'publish', webPublisher)
    root.putChild(b'', root)
    site = server.Site(root)
    reactor.listenTCP(12000, site)
    yield asleep(0.1)

    logging.info('Adding dbus signals')
    system = yield client.connect(reactor, 'system')
    controlApi = yield system.getRemoteObject('com.krontech.chronos.control', '/com/krontech/chronos/control')
    videoApi   = yield system.getRemoteObject('com.krontech.chronos.video',   '/com/krontech/chronos/video')
    ringApi    = None

    dbusSignalPublisher = dbusPublisher(subscribe, controlApi, videoApi, ringApi)

    logging.info('Adding methods')
    control = resource.Resource()
    root.putChild(b'control', control)
    Method(control, controlApi, 'getCameraData',            arguments=False)
    Method(control, controlApi, 'getSensorData',            arguments=False)
    Method(control, controlApi, 'status',                   arguments=False)
    Method(control, controlApi, 'reinitSystem',             arguments=True)
    Method(control, controlApi, 'getSensorCapabilities',    arguments=False)
    Method(control, controlApi, 'getSensorSettings',        arguments=False)
    Method(control, controlApi, 'getSensorLimits',          arguments=True)
    Method(control, controlApi, 'setSensorSettings',        arguments=True)
    Method(control, controlApi, 'setSensorTiming',          arguments=True)
    Method(control, controlApi, 'getIoCapabilities',        arguments=False)
    Method(control, controlApi, 'getIoMapping',             arguments=False)
    Method(control, controlApi, 'setIoMapping',             arguments=True)
    Method(control, controlApi, 'getCalCapabilities',       arguments=False)
    Method(control, controlApi, 'calibrate',                arguments=True)
    Method(control, controlApi, 'getColorMatrix',           arguments=False)
    Method(control, controlApi, 'setColorMatrix',           arguments=True)
    Method(control, controlApi, 'getWhiteBalance',          arguments=False)
    Method(control, controlApi, 'setWhiteBalance',          arguments=True) 
    Method(control, controlApi, 'getSequencerCapabilities', arguments=False)
    Method(control, controlApi, 'getSequencerProgram',      arguments=True)
    Method(control, controlApi, 'setSequencerProgram',      arguments=True)
    Method(control, controlApi, 'startRecord',              arguments=True)
    Method(control, controlApi, 'stopRecord',               arguments=True)

    video = resource.Resource()
    root.putChild(b'video', video)
    Method(video, videoApi, 'status',      arguments=False)
    Method(video, videoApi, 'flush',       arguments=False)
    Method(video, videoApi, 'playback',    arguments=True)
    Method(video, videoApi, 'configure',   arguments=True)
    Method(video, videoApi, 'livedisplay', arguments=False)
    Method(video, videoApi, 'recordfile',  arguments=True)
    Method(video, videoApi, 'stop',        arguments=False)
    Method(video, videoApi, 'overlay',     arguments=True)

    root.putChild(b'screenCap.jpg', previewImage(videoApi))
    root.putChild(b'liveImage.jpg', previewImage(videoApi))
    root.putChild(b'playbackImage.jpg', playbackImage(videoApi))

    root.putChild(b'waitForTouch', waitForTouch())
    root.putChild(b'waitForTouchThenBlackcal', waitForTouchThenBlackcal(controlApi))
    logging.info("All Systems Go")
    
if __name__ == "__main__":
    root = Root()
    logging.basicConfig(level=logging.DEBUG, format='%(asctime)s %(levelname)s [%(funcName)s] %(message)s')
    reactor.callWhenRunning( main )

    log.startLogging(sys.stdout)
    reactor.run()

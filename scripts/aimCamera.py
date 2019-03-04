#!/usr/bin/python
import numpy
import pychronos
import logging
import io
import urllib.parse

from twisted.web import server, resource
from twisted.web.client import getPage
from twisted.internet.defer import inlineCallbacks
from twisted.internet import reactor, defer, utils
from PIL import Image

from regmaps import zebra, display
import json

def asleep(secs):
    """
    @brief Do a reactor-safe sleep call. Call with yield to block until done.
    @param secs Time, in seconds
    @retval Deferred whose callback will fire after time has expired
    """
    d = defer.Deferred()
    reactor.callLater(secs, d.callback, None)
    return d

def allowCrossOrigin(request, methods='GET, POST, OPTION', contentType='application/json'):
    # Append headers to allow cross-origin requests.
    request.setHeader('Access-Control-Allow-Origin', '*')
    request.setHeader('Access-Control-Allow-Methods', methods)
    if ('POST' in methods):
        request.setHeader('Access-Control-Allow-Headers', 'Content-Type')
    request.setHeader('Content-Type', contentType)
    request.setHeader('Access-Control-Max-Age', 2520)

class aimCameraResource(resource.Resource):
    isLeaf = True
    def __init__(self):
        super().__init__()
        self.aimCamera = aimCamera()

    def allowCrossOrigin(self, request):
        # Append headers to allow cross-origin requests.
        request.setHeader('Access-Control-Allow-Origin', '*')
        request.setHeader('Access-Control-Allow-Methods', 'GET, POST, OPTION')
        request.setHeader('Access-Control-Allow-Headers', 'Content-Type')
        request.setHeader('Access-Control-Max-Age', 2520)

    def render_OPTIONS(self, request):
        allowCrossOrigin(request)
        request.setHeader('Content-Type', 'application/json')
        request.write('')
        request.finish()
        return server.NOT_DONE_YET
        
    def render_GET(self, request):
        self.allowCrossOrigin(request)
        request.setHeader('Content-Type', 'application/json')
        if request.args.get(b'disable', False):
            reactor.callLater(0.0, self.aimCamera.remove)
            return b'{"applied":false}'
        else:
            imageUrl = urllib.parse.unquote(request.args.get(b'imageUrl', b'')[0].decode('utf8'))
            if not imageUrl:
                imageUrl = 'http://www.wallpapergeeks.com/wp-content/uploads/2013/10/european-otter-wallpaper-1280x1024.jpg'
            zebra = request.args.get('zebra', 50)
            reactor.callLater(0.0, self.aimCamera.apply, imageUrl, zebra)
            return b'{"applied":true}'
            
    def render_POST(self, request):
        contentType = request.getHeader("Content-Type")
        self.allowCrossOrigin(request)
        if contentType == "application/json":
            # Expect a JSON object for the POST data.
            data = json.loads(request.content.getvalue().decode("utf8"))
        else:
            # Expect JSON data passed in URL-encoded form.
            rawData = request.args.get(b'data', None)
            if not rawData:
                request.setResponseCode(400)
                return b'"data" field required'
            data = ''
            for line in rawData:
                data += line.decode('utf8')
            data = json.loads(data)

        logging.debug("data: %s", data)
        request.setHeader('Content-Type', 'application/json')
        disable = data.get('disable', False)
        if disable:
            reactor.callLater(0.0, self.aimCamera.remove)
            return b'{"applied":false}'
        else:
            imageUrl = data.get('imageUrl', '')
            logging.debug('imageUrl: %s', imageUrl)
            if not imageUrl:
                imageUrl = 'http://www.wallpapergeeks.com/wp-content/uploads/2013/10/european-otter-wallpaper-1280x1024.jpg'
            zebra = data.get('zebra', 50)
            reactor.callLater(0.0, self.aimCamera.apply, imageUrl, zebra)
            return b'{"applied":true}'
        
        
class aimCamera(object):
    def __init__(self):
        self.applied = False

    @inlineCallbacks
    def bayerEncode(self, image, newData):
        shape = image.shape
        for row in range(shape[0]):
            yield asleep(0)
            for col in range(shape[1]):
                pos = ((row & 1) << 1) | (col & 1)
                if   (pos == 0): newData[row][col] = image[row][col][0]
                elif (pos == 1): newData[row][col] = image[row][col][1]
                elif (pos == 2): newData[row][col] = image[row][col][1]
                else:            newData[row][col] = image[row][col][2]
    
    @inlineCallbacks
    def apply(self, imageUrl, threshold):
        if (self.applied):
            logging.warn('aimCamera.apply called when already applied')
            return
        self.applied = True
        
        logging.debug('aimCamera.apply called with URL:"%s", threshold: %d', imageUrl, threshold)
        self.imageUrl = imageUrl
        self.threshold = threshold

        self.zebra = zebra()

        self.origThreshold = self.zebra.threshold

        self.display = display()
        self.xres = self.display.hRes
        self.yres = self.display.vRes
        yield asleep(0)
        self.origFpn = numpy.asarray(pychronos.readframe(0, self.xres, self.yres))
        yield asleep(0)

        try:
            rawImage = yield getPage(str.encode(imageUrl))
        except Exception as e:
            logging.error(e)
        
        im = Image.open(io.BytesIO(rawImage))
        yield asleep(0)
        data = numpy.asarray(im, dtype='uint16')
        yield asleep(0)

        newData = numpy.ndarray(shape=(data.shape[0], data.shape[1]), dtype='uint16') * 4
        yield self.bayerEncode(data, newData)
        
        self.newFpn = self.origFpn - newData
        yield asleep(0)
        
        pychronos.writeframe(0, self.newFpn)
        yield asleep(0)
    
        self.zebra.threshold = threshold

        logging.debug('aimCamera.apply finished')
        
    def remove(self):
        if (not self.applied):
            logging.warn('aimCamera.remove called without it being applied')
            return
        
        logging.debug('aimCamera.remove called')
        pychronos.writeframe(0, self.origFpn)

        self.zebra.threshold = self.origThreshold

        self.applied = False
        logging.debug('aimCamera.remove finished')

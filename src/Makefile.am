## The stuff we want to build.
noinst_LIBRARIES = libcamera.a
bin_PROGRAMS = cam-pipeline cam-pcUtil
bin_PROGRAMS += cam-loader cam-regdump cam-recover
bin_PROGRAMS += cam-json cam-listener cam-scgi
bin_PROGRAMS += fw-logger
AM_CFLAGS = -I ${srcdir}/lib
AM_CFLAGS += -Wno-deprecated-declarations
AM_LDFLAGS = -pthread
GLIB_LIBS += -lpcre

## Build the pipeline and FPGA daemons only for ARM targets.
if CAMBUILD
AM_CFLAGS += -mfloat-abi=softfp -mcpu=cortex-a8 -mfpu=neon
endif

## Handle cross compiler sysrooting to build against the ancient camera
if SYSROOT
AM_CFLAGS += -U_FORTIFY_SOURCE
AM_CFLAGS += --sysroot=${SYSROOT}
AM_LDFLAGS += --sysroot=${SYSROOT}
endif

## Bundle the common FPGA and image sensor tools into a library.
libcamera_a_CFLAGS = ${AM_CFLAGS} ${DBUS_CFLAGS}
libcamera_a_SOURCES = lib/board-chronos14.c
libcamera_a_SOURCES += lib/dbus-json.c
libcamera_a_SOURCES += lib/fpga-loader.c
libcamera_a_SOURCES += lib/fpga-mmap.c
libcamera_a_SOURCES += lib/edid.c
libcamera_a_SOURCES += lib/i2c-eeprom.c
libcamera_a_SOURCES += lib/i2c-spd.c
libcamera_a_SOURCES += lib/ioport.c
libcamera_a_SOURCES += lib/jsmn.c
libcamera_a_SOURCES += lib/lux1310-sensor.c
libcamera_a_SOURCES += lib/lux1310-wavetab.c
libcamera_a_SOURCES += lib/memcpy-neon.c
libcamera_a_SOURCES += lib/tiff.c
libcamera_a_SOURCES += lib/segment.c
libcamera_a_SOURCES += lib/sensor.c
## Header files too.
libcamera_a_SOURCES += lib/dbus-json.h
libcamera_a_SOURCES += lib/fpga.h
libcamera_a_SOURCES += lib/fpga-gpmc.h
libcamera_a_SOURCES += lib/sensor-lux1310.h
libcamera_a_SOURCES += lib/edid.h
libcamera_a_SOURCES += lib/i2c.h
libcamera_a_SOURCES += lib/i2c-spd.h
libcamera_a_SOURCES += lib/ioport.h
libcamera_a_SOURCES += lib/jsmn.h
libcamera_a_SOURCES += lib/segment.h
## ARM-Only sources
if SYSROOT
libcamera_a_SOURCES += lib/glibc-hacks.c
endif

## FPGA and Image Sensor register dump tool
cam_regdump_LDADD = libcamera.a
cam_regdump_CFLAGS = ${AM_CFLAGS}
cam_regdump_LDFLAGS = ${AM_LDFLAGS}
cam_regdump_SOURCES = regdump/cam-regdump.c
cam_regdump_SOURCES += regdump/regs.h
cam_regdump_SOURCES += regdump/regs-fpga.c
cam_regdump_SOURCES += regdump/regs-luxima.c
cam_regdump_SOURCES += regdump/regs-lux1310.c
cam_regdump_SOURCES += regdump/regs-lux2100.c

## FPGA Video memory recovery.
cam_recover_LDADD = libcamera.a
cam_recover_CFLAGS = ${AM_CFLAGS}
cam_recover_LDFLAGS = ${AM_LDFLAGS}
cam_recover_SOURCES = cam-recover.c

## Camera DBus JSON translator tools
cam_json_LDADD = ${DBUS_LIBS} ${GLIB_LIBS} libcamera.a
cam_json_CFLAGS = ${AM_CFLAGS} ${DBUS_CFLAGS}
cam_json_LDFLAGS = ${AM_LDFLAGS}
cam_json_SOURCES = client/cam-json.c
cam_listener_LDADD = ${DBUS_LIBS} ${GLIB_LIBS} libcamera.a
cam_listener_CFLAGS = ${AM_CFLAGS} ${DBUS_CFLAGS}
cam_listener_LDFLAGS = ${AM_LDFLAGS}
cam_listener_SOURCES = client/cam-listener.c
cam_scgi_LDADD = ${DBUS_LIBS} ${GLIB_LIBS} ${XML_LIBS} libcamera.a
cam_scgi_CFLAGS = ${AM_CFLAGS} ${DBUS_CFLAGS} ${XML_CFLAGS}
cam_scgi_LDFLAGS = ${AM_LDFLAGS}
cam_scgi_SOURCES = scgi/main.c
cam_scgi_SOURCES += scgi/scgi.c
cam_scgi_SOURCES += scgi/introspect.c
cam_scgi_SOURCES += scgi/methods.c

## CLI Program to load the FPGA image and setup peripherals.
## TODO: This should ultimately be moved into a kernel module.
cam_loader_LDADD = libcamera.a
cam_loader_CFLAGS = ${AM_CFLAGS} ${DBUS_CFLAGS}
cam_loader_LDFLAGS = ${AM_LDFLAGS}
cam_loader_SOURCES = cam-loader.c

## GStreamer Daemon for running the video system.
cam_pipeline_LDADD = libcamera.a
cam_pipeline_LDADD += ${DBUS_LIBS} ${GLIB_LIBS} ${GST_LIBS} -lpcre
cam_pipeline_CFLAGS = ${AM_CFLAGS} ${DBUS_CFLAGS} ${GST_CFLAGS}
cam_pipeline_LDFLAGS = ${AM_LDFLAGS} -pthread
cam_pipeline_LDADD += -ljpeg -lrt
cam_pipeline_SOURCES = pipeline/cam-pipeline.c
cam_pipeline_SOURCES += pipeline/audiomux.c
cam_pipeline_SOURCES += pipeline/dbus-params.c
cam_pipeline_SOURCES += pipeline/dbus-video.c
cam_pipeline_SOURCES += pipeline/dng.c
cam_pipeline_SOURCES += pipeline/h264.c
cam_pipeline_SOURCES += pipeline/hdmi.c
cam_pipeline_SOURCES += pipeline/lcd.c
cam_pipeline_SOURCES += pipeline/overlay.c
cam_pipeline_SOURCES += pipeline/playback.c
cam_pipeline_SOURCES += pipeline/raw.c
cam_pipeline_SOURCES += pipeline/rtsp-server.c
cam_pipeline_SOURCES += pipeline/rtsp-methods.c
cam_pipeline_SOURCES += pipeline/rtsp-private.h
cam_pipeline_SOURCES += pipeline/screencap.c
cam_pipeline_SOURCES += pipeline/pipeline.h
# Private gstreamer elements.
cam_pipeline_SOURCES += pipeline/gst/gifdec.c
cam_pipeline_SOURCES += pipeline/gst/gifdec.h
cam_pipeline_SOURCES += pipeline/gst/gstgifsrc.c
cam_pipeline_SOURCES += pipeline/gst/gstgifsrc.h
cam_pipeline_SOURCES += pipeline/gst/gstneon.c
cam_pipeline_SOURCES += pipeline/gst/gstneoncrop.c
cam_pipeline_SOURCES += pipeline/gst/gstneonflip.c
cam_pipeline_SOURCES += pipeline/gst/gstneon.h

## Firmware logger for ti81xx video coprocessor debugging.
fw_logger_CFLAGS = ${AM_CFLAGS} -I/usr/include/ti81xx
fw_logger_SOURCES = fwlogger/loggerSMDump.c
fw_logger_SOURCES += fwlogger/LoggerSM.c

## Power control daemon
cam_pcUtil_CFLAGS = ${AM_CFLAGS} ${DBUS_CFLAGS}
cam_pcUtil_LDFLAGS = ${AM_LDFLAGS} 
cam_pcUtil_SOURCES = pcUtil/pcutil.c
cam_pcUtil_SOURCES += pcUtil/pcutil.h
cam_pcUtil_SOURCES += pcUtil/comms.c
cam_pcUtil_SOURCES += pcUtil/comms.h
cam_pcUtil_SOURCES += pcUtil/intelHex.c
cam_pcUtil_SOURCES += pcUtil/IntelHex.h
cam_pcUtil_SOURCES += pcUtil/MBCRC16.c
cam_pcUtil_SOURCES += pcUtil/MBCRC16.h
cam_pcUtil_SOURCES += pcUtil/types.h
cam_pcUtil_SOURCES += pcUtil/socket.c
cam_pcUtil_SOURCES += pcUtil/socket.h

##
## Autogenerated sources for DBus marshalling
##
EXTRA_DIST = api/ca.krontech.chronos.control.xml
BUILT_SOURCES = api/cam-dbus-client.h api/cam-dbus-server.h api/cam-dbus-video.h

$(srcdir)/api/cam-dbus-client.h: api/ca.krontech.chronos.control.xml
	dbus-binding-tool --prefix=cam_dbus --mode=glib-client $< > $@

$(srcdir)/api/cam-dbus-server.h: api/ca.krontech.chronos.control.xml
	dbus-binding-tool --prefix=cam_control --mode=glib-server $< > $@

$(srcdir)/api/cam-dbus-video.h: api/ca.krontech.chronos.video.xml
	dbus-binding-tool --prefix=cam_video --mode=glib-server $< > $@


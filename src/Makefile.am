## The stuff we want to build.
noinst_LIBRARIES = libcamera.a
bin_PROGRAMS = cam-cli cam-loader cam-pipeline

## Bundle the common FPGA and image sensor tools into a library.
libcamera_a_SOURCES = fpga-loader.c
libcamera_a_SOURCES += fpga-mmap.c
libcamera_a_SOURCES += fpga-lux1310.c
libcamera_a_SOURCES += i2c-eeprom.c
libcamera_a_SOURCES += i2c-spd.c

## CLI Program for manipulating the camera and image sensor.
cam_cli_LDADD = libcamera.a
cam_cli_SOURCES = cam-cli.c
cam_cli_SOURCES += cli/cli-table.c
cam_cli_SOURCES += cli/cli-help.c
cam_cli_SOURCES += cli/cli-fpgaload.c
cam_cli_SOURCES += cli/cli-info.c
cam_cli_SOURCES += cli/cli-led.c
cam_cli_SOURCES += cli/cli-lux1310.c

## CLI Program to load the FPGA image and setup peripherals.
## TODO: This should ultimately be moved into a kernel module.
cam_loader_LDADD = libcamera.a
cam_loader_SOURCES = cam-loader.c

## GStreamer Daemon for running the video system.
cam_pipeline_LDADD = libcamera.a
cam_pipeline_CFLAGS = -I ${prefix}/usr/include/gstreamer-0.10
cam_pipeline_CFLAGS += -I ${prefix}/usr/include/glib-2.0
cam_pipeline_CFLAGS += -I ${prefix}/usr/lib/glib-2.0/include
cam_pipeline_LDFLAGS = -L ${prefix}/usr/lib -L ${prefix}/usr/lib/gstreamer-0.10
cam_pipeline_LDADD +=  -ldl -lgmodule-2.0 -lgobject-2.0 -lgstbase-0.10 -lgstreamer-0.10 -lm -lpthread -l:libxml2.so.2 -l:libz.so.1 -lgthread-2.0 -lrt -lglib-2.0
cam_pipeline_SOURCES = cam-pipeline.c

#!/bin/sh

gst-launch \
    omx_camera \
        input-interface=VIP1_PORTA \
        capture-mode=SC_DISCRETESYNC_ACTVID_VSYNC \
        vif-mode=24BIT output-buffers=10 \
        skip-frames=0 ! \
    "video/x-raw-yuv, format=(fourcc)NV12, width=1280, height=1024, framerate=60/1, buffer-count-requested=4" ! \
    omx_mdeiscaler name=d d.src_00 ! \
    'video/x-raw-yuv, width=(int)800, height=(int)480' ! \
    omx_ctrl display-mode=OMX_DC_MODE_1080P_60 display-device=LCD ! \
    gstperf print-fps=true print-arm-load=true  ! \
    omx_videosink display-device=LCD sync=false d.src_00

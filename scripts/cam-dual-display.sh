#!/bin/sh

gst-launch \
    omx_camera \
        input-interface=VIP1_PORTA \
        capture-mode=SC_DISCRETESYNC_ACTVID_VSYNC \
        vif-mode=24BIT output-buffers=10 \
        skip-frames=0 ! \
    "video/x-raw-yuv, format=(fourcc)NV12, width=1280, height=720, framerate=60/1, buffer-count-requested=4" ! \
    tee name=t ! \
        queue ! \
        omx_mdeiscaler name=d d.src_00 ! \
        'video/x-raw-yuv, width=(int)1920, height=(int)1080' ! \
        omx_ctrl display-mode=OMX_DC_MODE_1080P_60 display-device=HDMI ! \
        omx_videosink display-mode=OMX_DC_MODE_1080P_60 display-device=HDMI sync=false \
    t. ! \
        queue ! \
        omx_scaler ! \
        'video/x-raw-yuv, width=(int)800, height=(int)480' ! \
        omx_ctrl display-mode=OMX_DC_MODE_1080P_60 display-device=LCD ! \
        omx_videosink display-mode=OMX_DC_MODE_1080P_60 display-device=LCD sync=false

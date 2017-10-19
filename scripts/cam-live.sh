#!/bin/sh
modprobe ti81xxvo

gst-launch \
    omx_camera \
        input-interface=VIP1_PORTA \
        capture-mode=SC_DISCRETESYNC_ACTVID_VSYNC \
        vif-mode=24BIT \
        output-buffers=10 \
        skip-frames=0 ! \
    "video/x-raw-yuv, format=(fourcc)NV12, width=1280, height=1024, framerate=60/1, buffer-count-requested=4" ! \
    tee name=t ! \
        queue ! \
        omx_mdeiscaler \
        name=d d.src_00 ! \
        'video/x-raw-yuv, width=(int)1280, height=(int)1024' ! \
        gstperf print-fps=true print-arm-load=true ! \
        v4l2sink sync=false device=/dev/video1 \
    t. ! \
        queue ! \
        omx_scaler ! \
        'video/x-raw-yuv, width=800, height=480' ! \
        v4l2sink sync=false device=/dev/video2

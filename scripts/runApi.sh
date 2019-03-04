#!/bin/bash
cd /root/api/
killall python
sleep 0.5s
nohup python controlDbus.py &> /var/volatile/controlDbus.out &
sleep 4.0s
nohup python dbusWebBridge.py &> /var/volatile/dbusWebBridge.out &

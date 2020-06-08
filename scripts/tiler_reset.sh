#!/bin/bash
bash -c "service lightdm stop; \
echo 0 > /sys/class/vtconsole/vtcon1/bind; \
rmmod -f omapdrm; \
sleep 1; \
modprobe omapdrm; \
service lightdm start \
" &
disown -h



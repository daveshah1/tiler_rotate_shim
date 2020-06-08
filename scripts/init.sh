#!/bin/bash

# At least one should work, depending on kernel
modprobe pvrsrvkm_omap5_sgx544_116
modprobe pvrsrvkm_omap_omap5_sgx544_116

echo "[default]" | sudo tee /etc/powervr.ini
echo "WindowSystem=libpvrDRMWSEGL.so" | sudo tee -a /etc/powervr.ini
echo "DefaultPixelFormat=RGB888" | sudo tee -a /etc/powervr.ini

sudo /opt/omap5-sgx-ddk-um-linux/bin/pvrsrvctl --start --no-module



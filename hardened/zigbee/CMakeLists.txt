# Spooky Lights, hardened ESP-IDF build (zigbee). Build with ../build.sh, not idf.py
# directly: the script adds the security config and your signing key path.
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# NanoC6 / NanoH2 talk over the chip's built-in USB-Serial-JTAG: Serial = USB
# (the Arduino IDE's "USB CDC On Boot: Enabled").
idf_build_set_property(COMPILE_OPTIONS "-DARDUINO_USB_MODE=1;-DARDUINO_USB_CDC_ON_BOOT=1" APPEND)
# The sketch picks its Zigbee transport from this (Arduino IDE: Zigbee mode = ZCZR).
idf_build_set_property(COMPILE_OPTIONS "-DZIGBEE_MODE_ZCZR" APPEND)

project(spooky_lights)

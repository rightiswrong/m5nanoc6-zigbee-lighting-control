# Spooky Lights, hardened ESP-IDF build (matter). Build with ../build.sh, not idf.py
# directly: the script adds the security config and your signing key path.
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# NanoC6 / NanoH2 talk over the chip's built-in USB-Serial-JTAG: Serial = USB
# (the Arduino IDE's "USB CDC On Boot: Enabled").
idf_build_set_property(COMPILE_OPTIONS "-DARDUINO_USB_MODE=1;-DARDUINO_USB_CDC_ON_BOOT=1" APPEND)
idf_build_set_property(CXX_COMPILE_OPTIONS "-std=gnu++2a;-DCHIP_HAVE_CONFIG_H" APPEND)
# RISC-V: project_include.cmake sets -Wno-format but not the flags that depend on it
idf_build_set_property(COMPILE_OPTIONS "-Wno-format-nonliteral;-Wno-format-security" APPEND)
if(SPOOKY_WIFI)
  idf_build_set_property(COMPILE_OPTIONS "-DMATTER_USE_WIFI=1" APPEND)
endif()

project(spooky_lights)

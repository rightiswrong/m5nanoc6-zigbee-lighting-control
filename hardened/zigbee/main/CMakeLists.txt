# The firmware sources are the same files the Arduino IDE builds.
set(fw "${CMAKE_CURRENT_LIST_DIR}/../../../firmware/SpookyLights")
file(GLOB fw_srcs "${fw}/*.cpp" "${fw}/*.c")
idf_component_register(SRCS "main.cpp" ${fw_srcs}
                       PRIV_INCLUDE_DIRS "${fw}")

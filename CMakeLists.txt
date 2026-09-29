# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/lib")

# Khoa ThingSpeak that nam trong sdkconfig.secrets (khong commit).
# Van ton trong -D SDKCONFIG_DEFAULTS=... / bien moi truong neu nguoi dung tu truyen.
if(DEFINED SDKCONFIG_DEFAULTS OR DEFINED ENV{SDKCONFIG_DEFAULTS})
    # giu nguyen
elseif(EXISTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.secrets")
    set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;sdkconfig.secrets")
else()
    set(SDKCONFIG_DEFAULTS "sdkconfig.defaults")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32-bmp180-dht11)

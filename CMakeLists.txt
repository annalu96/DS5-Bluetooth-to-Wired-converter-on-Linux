cmake_minimum_required(VERSION 3.16)
project(Pico2W_DualSense5_Bridge_Linux)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add dependencies
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)

# The opus library
pkg_check_modules(OPUS REQUIRED opus)
pkg_check_modules(BLUEZ REQUIRED bluez)

# Audio resampler include
include_directories(lib/WDL/WDL)

add_executable(dualsense_bridge
    src/main.cpp
    src/audio.cpp
    src/usb.cpp
    src/bt.cpp
    lib/WDL/WDL/resample.cpp
)

target_include_directories(dualsense_bridge PRIVATE
    src
    ${OPUS_INCLUDE_DIRS}
    ${BLUEZ_INCLUDE_DIRS}
)

target_link_libraries(dualsense_bridge PRIVATE
    Threads::Threads
    ${OPUS_LIBRARIES}
    ${BLUEZ_LIBRARIES}
)

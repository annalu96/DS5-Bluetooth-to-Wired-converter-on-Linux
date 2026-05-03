cmake_minimum_required(VERSION 3.16)
project(Pico2W_DualSense5_Bridge_Linux)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add dependencies
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)

# The opus library
pkg_check_modules(OPUS REQUIRED opus)
pkg_check_modules(ALSA REQUIRED alsa)

# Audio resampler include
include_directories(lib/WDL/WDL)

add_executable(dualsense_bridge
    src/main.cpp
    src/audio.cpp
    src/usb.cpp
    src/bt.cpp
    src/usb_descriptors.cpp
    lib/WDL/WDL/resample.cpp
)

target_include_directories(dualsense_bridge PRIVATE
    src
    ${OPUS_INCLUDE_DIRS}
    ${ALSA_INCLUDE_DIRS}
)

target_link_libraries(dualsense_bridge PRIVATE
    Threads::Threads
    ${OPUS_LIBRARIES}
    ${ALSA_LIBRARIES}
)

# Copy shell scripts to build directory so the daemon finds the latest versions
add_custom_command(TARGET dualsense_bridge POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/setup_gadget.sh
        ${CMAKE_CURRENT_BINARY_DIR}/setup_gadget.sh
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/teardown_gadget.sh
        ${CMAKE_CURRENT_BINARY_DIR}/teardown_gadget.sh
    COMMAND chmod +x
        ${CMAKE_CURRENT_BINARY_DIR}/setup_gadget.sh
        ${CMAKE_CURRENT_BINARY_DIR}/teardown_gadget.sh
    COMMENT "Copying gadget scripts to build directory"
)

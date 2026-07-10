cmake_minimum_required(VERSION 3.10)
project(RobotVision)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(OpenCV REQUIRED)

# ⭐ 迈德威视 SDK 路径
set(MINDVISION_SDK_DIR "/home/c349/桌面/linuxSDK_V2.1.0.49202602041120")

include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${OpenCV_INCLUDE_DIRS}
    ${MINDVISION_SDK_DIR}/include
    /usr/include/openvino
)

link_directories(
    ${MINDVISION_SDK_DIR}/lib/x64
    /usr/lib/x86_64-linux-gnu
)

add_executable(robot_vision
    src/main.cpp
    src/RobotVision.cpp
    src/VisionController.cpp
    src/SerialPort.cpp
    src/MindVisionCamera.cpp
)

target_link_libraries(robot_vision
    ${OpenCV_LIBS}
    pthread
    MVSDK
    openvino
)

set_target_properties(robot_vision PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
)
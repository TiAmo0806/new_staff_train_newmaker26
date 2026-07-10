cmake_minimum_required(VERSION 3.10)
project(RobotVision)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(OpenCV REQUIRED)

include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${OpenCV_INCLUDE_DIRS}
)

add_executable(robot_vision
    src/main.cpp
    src/RobotVision.cpp
    src/VisionController.cpp
    src/SerialPort.cpp
)

target_link_libraries(robot_vision
    ${OpenCV_LIBS}
    onnxruntime
    pthread
)

set_target_properties(robot_vision PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
)

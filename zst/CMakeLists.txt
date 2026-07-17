cmake_minimum_required(VERSION 3.10)
project(logistics_vision LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()


find_package(OpenCV REQUIRED)
find_package(yaml-cpp REQUIRED)

# OpenVINO Runtime
#
# 程序直接使用 OpenVINO C++ API 读取 best5.onnx。首次运行时 OpenVINO 会完成图优化和
# 设备编译，并把编译结果写入配置指定的缓存目录；后续启动可直接复用缓存。
# Ubuntu/NUC 如果 find_package 找不到 OpenVINO，请先执行：
#   source /opt/intel/openvino/setupvars.sh
# 或配置时显式指定：
#   cmake .. -DOpenVINO_DIR=/实际路径/runtime/cmake
find_package(OpenVINO REQUIRED)


# MindVision 工业相机 SDK
# 不把用户名或固定 /home 路径写死在工程中。
set(MINDVISION_ROOT "$ENV{MINDVISION_ROOT}"
    CACHE PATH "MindVision Linux SDK installation root")

file(GLOB MINDVISION_SIBLING_DIRS
    LIST_DIRECTORIES true
    "${CMAKE_CURRENT_SOURCE_DIR}/../linuxSDK*"
)

find_path(MINDVISION_INCLUDE_DIR
    NAMES CameraApi.h
    HINTS
        "${MINDVISION_ROOT}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../linuxSDK"
        ${MINDVISION_SIBLING_DIRS}
    PATH_SUFFIXES include
)

find_library(MINDVISION_LIBRARY
    NAMES MVSDK libMVSDK
    HINTS
        "${MINDVISION_ROOT}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../linuxSDK"
        ${MINDVISION_SIBLING_DIRS}
    PATH_SUFFIXES lib/x64 lib/amd64 lib lib64
)

if(NOT MINDVISION_INCLUDE_DIR)
    message(FATAL_ERROR
        "找不到 CameraApi.h。请使用：\n"
        "  cmake .. -DMINDVISION_ROOT=/你的/MindVision-Linux-SDK根目录\n"
        "该根目录下面应包含 include/CameraApi.h")
endif()

if(NOT MINDVISION_LIBRARY)
    message(FATAL_ERROR
        "找不到 libMVSDK.so。请检查 MINDVISION_ROOT 下的 lib/x64、lib 或 lib64")
endif()

message(STATUS "OpenCV version: ${OpenCV_VERSION}")
message(STATUS "OpenVINO Runtime: found")
message(STATUS "MindVision include: ${MINDVISION_INCLUDE_DIR}")
message(STATUS "MindVision library: ${MINDVISION_LIBRARY}")

set(ALL_SOURCES
    src/CameraDriver/MindVisionCamera.cpp
    src/Communication/CRC16.cpp
    src/Communication/VirtualSerial.cpp
    src/Communication/VisionProtocol.cpp
    src/ImgProcessing/CompetitionWorkflow.cpp
    src/ImgProcessing/FieldStateCollector.cpp
    src/ImgProcessing/TaskPlanner.cpp
    src/ImgProcessing/VisionTypes.cpp
    src/ImgProcessing/VisionSystem.cpp
    src/ImgProcessing/YoloOpenVinoDetector.cpp
    src/Tool/Utils.cpp
    main.cpp
)

add_executable(logistics_vision ${ALL_SOURCES})

# ============================================================================
# 头文件路径
# ============================================================================
# 项目内部统一使用相对于 include/ 的路径，例如：
#   #include "ImgProcessing/VisionSystem.h"
#   #include "CameraDriver/MindVisionCamera.h"
# OpenVINO头文件由openvino::runtime目标自动注入；CameraApi.h由相机SDK目录注入。
target_include_directories(logistics_vision PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${OpenCV_INCLUDE_DIRS}
    ${MINDVISION_INCLUDE_DIR}
)

target_link_libraries(logistics_vision PRIVATE
    ${OpenCV_LIBS}
    yaml-cpp
    openvino::runtime
    ${MINDVISION_LIBRARY}
    pthread
    dl
)

# 相机SDK不是CMake导入目标，显式加入RPATH以便运行时找到libMVSDK.so。
# OpenVINO的运行库路径由openvino::runtime导入目标负责。
get_filename_component(MINDVISION_LIB_DIR "${MINDVISION_LIBRARY}" DIRECTORY)
set_target_properties(logistics_vision PROPERTIES
    BUILD_RPATH "${MINDVISION_LIB_DIR}"
)

message(STATUS "")
message(STATUS "Build:")
message(STATUS "  mkdir -p build")
message(STATUS "  cd build")
message(STATUS "  cmake ..")
message(STATUS "  make -j")
message(STATUS "")
message(STATUS "Run:")
message(STATUS "  ./logistics_vision ../config/vision.yaml")
message(STATUS "")

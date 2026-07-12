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


# ONNX Runtime
#
# 不把任何用户名或 /home 路径写死在工程中。查找优先级：
#   1) cmake -DONNXRUNTIME_ROOT=/实际目录
#   2) 环境变量 ONNXRUNTIME_ROOT
#   3) 工程同级的 onnxruntime / onnxruntime-linux-x64-* 目录
#   4) CMake 默认系统搜索路径（/usr、/usr/local 等）
set(ONNXRUNTIME_ROOT "$ENV{ONNXRUNTIME_ROOT}"
    CACHE PATH "ONNX Runtime installation root")

file(GLOB ONNXRUNTIME_SIBLING_DIRS
    LIST_DIRECTORIES true
    "${CMAKE_CURRENT_SOURCE_DIR}/../onnxruntime*"
)

find_path(ONNXRUNTIME_INCLUDE_DIR
    NAMES onnxruntime_cxx_api.h
    HINTS
        "${ONNXRUNTIME_ROOT}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../onnxruntime"
        ${ONNXRUNTIME_SIBLING_DIRS}
    PATH_SUFFIXES
        include
        include/onnxruntime
        include/onnxruntime/core/session
)

find_library(ONNXRUNTIME_LIBRARY
    NAMES onnxruntime libonnxruntime
    HINTS
        "${ONNXRUNTIME_ROOT}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../onnxruntime"
        ${ONNXRUNTIME_SIBLING_DIRS}
    PATH_SUFFIXES lib lib64
)

if(NOT ONNXRUNTIME_INCLUDE_DIR)
    message(FATAL_ERROR
        "找不到 onnxruntime_cxx_api.h。请使用：\n"
        "  cmake .. -DONNXRUNTIME_ROOT=/你的/onnxruntime根目录\n"
        "该根目录下面应包含 include/onnxruntime_cxx_api.h")
endif()

if(NOT ONNXRUNTIME_LIBRARY)
    message(FATAL_ERROR
        "找不到 libonnxruntime.so。请检查 ONNXRUNTIME_ROOT 下是否存在 lib/ 或 lib64/")
endif()


# MindVision 工业相机 SDK
# 查找方式与 ONNX Runtime 相同：命令行/环境变量优先，其次工程同级目录和系统目录。
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
message(STATUS "ONNX Runtime include: ${ONNXRUNTIME_INCLUDE_DIR}")
message(STATUS "ONNX Runtime library: ${ONNXRUNTIME_LIBRARY}")
message(STATUS "MindVision include: ${MINDVISION_INCLUDE_DIR}")
message(STATUS "MindVision library: ${MINDVISION_LIBRARY}")

set(ALL_SOURCES
    src/CameraDriver/MindVisionCamera.cpp
    src/Communication/ByteConverter.cpp
    src/Communication/CRC16.cpp
    src/Communication/VirtualSerial.cpp
    src/Communication/VisionProtocol.cpp
    src/ImgProcessing/BeanSvmClassifier.cpp
    src/ImgProcessing/CompetitionWorkflow.cpp
    src/ImgProcessing/FieldStateCollector.cpp
    src/ImgProcessing/TaskPlanner.cpp
    src/ImgProcessing/VisionTypes.cpp
    src/ImgProcessing/VisionSystem.cpp
    src/ImgProcessing/YoloOrtDetector.cpp
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
# 第三方头文件使用 <CameraApi.h> / <onnxruntime_cxx_api.h>，
# 实际目录由下面的 MINDVISION_INCLUDE_DIR / ONNXRUNTIME_INCLUDE_DIR 注入。
target_include_directories(logistics_vision PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${OpenCV_INCLUDE_DIRS}
    ${ONNXRUNTIME_INCLUDE_DIR}
    ${MINDVISION_INCLUDE_DIR}
)

target_link_libraries(logistics_vision PRIVATE
    ${OpenCV_LIBS}
    yaml-cpp
    ${ONNXRUNTIME_LIBRARY}
    ${MINDVISION_LIBRARY}
    pthread
    dl
)

# 运行时能找到 .so，减少手动 export LD_LIBRARY_PATH 的麻烦。
get_filename_component(ONNXRUNTIME_LIB_DIR "${ONNXRUNTIME_LIBRARY}" DIRECTORY)
get_filename_component(MINDVISION_LIB_DIR "${MINDVISION_LIBRARY}" DIRECTORY)
set_target_properties(logistics_vision PROPERTIES
    BUILD_RPATH "${ONNXRUNTIME_LIB_DIR};${MINDVISION_LIB_DIR}"
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

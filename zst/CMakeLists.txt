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

set(ONNXRUNTIME_ROOT
    "$ENV{ONNXRUNTIME_ROOT}"
    CACHE PATH "ONNX Runtime root directory")

if(NOT ONNXRUNTIME_ROOT)
    set(ONNXRUNTIME_ROOT
        "/home/zst/onnxruntime")
endif()

find_path(ONNXRUNTIME_INCLUDE_DIR
    NAMES onnxruntime_cxx_api.h
    PATHS
        "${ONNXRUNTIME_ROOT}/include"
        "/usr/local/include/onnxruntime"
        "/usr/local/include"
        "/opt/onnxruntime/include"
    NO_DEFAULT_PATH)

find_library(ONNXRUNTIME_LIBRARY
    NAMES onnxruntime libonnxruntime
    PATHS
        "${ONNXRUNTIME_ROOT}/lib"
        "/usr/local/lib"
        "/usr/lib"
        "/opt/onnxruntime/lib"
    NO_DEFAULT_PATH)

if(NOT ONNXRUNTIME_INCLUDE_DIR)
    message(FATAL_ERROR "找不到 onnxruntime_cxx_api.h，请设置 ONNXRUNTIME_ROOT")
endif()

if(NOT ONNXRUNTIME_LIBRARY)
    message(FATAL_ERROR "找不到 libonnxruntime.so，请设置 ONNXRUNTIME_ROOT")
endif()


# MindVision 工业相机 SDK

set(MINDVISION_ROOT
    "$ENV{MINDVISION_ROOT}"
    CACHE PATH "MindVision SDK root directory")

if(NOT MINDVISION_ROOT)
    set(MINDVISION_ROOT "/home/zst/linuxSDK")
endif()

find_path(MINDVISION_INCLUDE_DIR
    NAMES CameraApi.h
    PATHS
        "${MINDVISION_ROOT}/include"
        "/usr/local/include"
        "/usr/include"
        "/opt/MindVision/include"
    NO_DEFAULT_PATH)

find_library(MINDVISION_LIBRARY
    NAMES MVSDK libMVSDK
    PATHS
        "${MINDVISION_ROOT}/lib/x64"
        "${MINDVISION_ROOT}/lib"
        "/usr/local/lib"
        "/usr/lib"
        "/opt/MindVision/lib/x64"
        "/opt/MindVision/lib"
    NO_DEFAULT_PATH)

if(NOT MINDVISION_INCLUDE_DIR)
    message(FATAL_ERROR "找不到 CameraApi.h，请设置 MINDVISION_ROOT")
endif()

if(NOT MINDVISION_LIBRARY)
    message(FATAL_ERROR "找不到 libMVSDK.so，请设置 MINDVISION_ROOT")
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
    src/ImgProcessing/BeanSvmClassifier.cpp
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
# 所有项目内部头文件使用绝对路径，例如：
#   #include "/home/zst/zst/include/ImgProcessing/VisionSystem.h"
#   #include "/home/zst/zst/include/CameraDriver/MindVisionCamera.h"
target_include_directories(logistics_vision PRIVATE
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

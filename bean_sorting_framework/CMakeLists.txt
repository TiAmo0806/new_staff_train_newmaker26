cmake_minimum_required(VERSION 3.10)
project(BeanSortingFramework VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ---- Release 优化 (树莓派必开) ----
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -mcpu=native -mtune=native")
set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG -mcpu=native -mtune=native")

find_package(OpenCV REQUIRED)
find_package(yaml-cpp REQUIRED)

# OpenVINO Runtime — 自动定位 (支持 apt / pip / Intel 官方包)
if(NOT OpenVINO_DIR)
    # 1. 尝试通过 Python 找到 pip 安装的 OpenVINO
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_FOUND)
        execute_process(
            COMMAND ${Python3_EXECUTABLE} -c
            "from pathlib import Path; import openvino; print(Path(openvino.__path__[0])/'cmake')"
            OUTPUT_VARIABLE _ov_pip_cmake
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(EXISTS "${_ov_pip_cmake}/OpenVINOConfig.cmake")
            set(OpenVINO_DIR "${_ov_pip_cmake}")
        endif()
    endif()

    # 2. 如果 pip 没找到，尝试系统默认路径
    if(NOT OpenVINO_DIR)
        set(OpenVINO_DIR "/opt/intel/openvino/runtime/cmake")
    endif()

    # 3. 提示用户手动指定
    if(NOT EXISTS "${OpenVINO_DIR}/OpenVINOConfig.cmake")
        message(FATAL_ERROR
            "找不到 OpenVINO，请用以下方式之一指定:\n"
            "  pip安装: 确认已执行 pip install openvino\n"
            "  手动指定: cmake .. -DOpenVINO_DIR=/你的路径/openvino/cmake")
    endif()
endif()

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
        "${CMAKE_CURRENT_SOURCE_DIR}/SDK"
        "${CMAKE_CURRENT_SOURCE_DIR}/../linuxSDK"
        ${MINDVISION_SIBLING_DIRS}
    PATH_SUFFIXES include
)

find_library(MINDVISION_LIBRARY
    NAMES MVSDK libMVSDK
    HINTS
        "${MINDVISION_ROOT}"
        "${CMAKE_CURRENT_SOURCE_DIR}/SDK"
        "${CMAKE_CURRENT_SOURCE_DIR}/../linuxSDK"
        ${MINDVISION_SIBLING_DIRS}
    PATH_SUFFIXES lib/x64 lib/amd64 lib lib64 lib/aarch64
)

if(NOT MINDVISION_INCLUDE_DIR)
    message(FATAL_ERROR
        "找不到 CameraApi.h。请使用：\n"
        "  cmake .. -DMINDVISION_ROOT=/你的/MindVision-Linux-SDK根目录\n"
        "该根目录下面应包含 include/CameraApi.h")
endif()

if(NOT MINDVISION_LIBRARY)
    message(FATAL_ERROR
        "找不到 libMVSDK.so。请检查 MINDVISION_ROOT 下的 lib/x64、lib、lib64 或 lib/aarch64")
endif()

message(STATUS "OpenCV version: ${OpenCV_VERSION}")
message(STATUS "OpenVINO Runtime: found")
message(STATUS "MindVision include: ${MINDVISION_INCLUDE_DIR}")
message(STATUS "MindVision library: ${MINDVISION_LIBRARY}")

set(ALL_SOURCES
    CameraDriver/MvCamera.cpp
    Communication/CRC16.cpp
    Communication/ByteConverter.cpp
    Communication/BeanSerial.cpp
    Detector/BeanDetector.cpp
    main.cpp
)

add_executable(bean_sorting ${ALL_SOURCES})

# ============================================================================
# 头文件路径
# ============================================================================
# 项目内部统一使用相对于项目根目录的路径，例如：
#   #include "Communication/BeanProtocol.h"
#   #include "CameraDriver/MvCamera.h"
# OpenVINO头文件由openvino::runtime目标自动注入；CameraApi.h由相机SDK目录注入。
target_include_directories(bean_sorting PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${OpenCV_INCLUDE_DIRS}
    ${MINDVISION_INCLUDE_DIR}
)

target_compile_definitions(bean_sorting PRIVATE HAS_MV_CAMERA)

target_link_libraries(bean_sorting PRIVATE
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
set_target_properties(bean_sorting PROPERTIES
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
message(STATUS "  ./bean_sorting ../config/vision.yaml")
message(STATUS "")

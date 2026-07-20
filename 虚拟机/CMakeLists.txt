cmake_minimum_required(VERSION 3.14)
project(mvs_openvino_demo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ============================================================
# OpenCV
# ============================================================
find_package(OpenCV REQUIRED)

# ============================================================
# OpenVINO Runtime
#
# 优先使用 find_package（官方安装/setupvars.sh），找不到时
# 自动扫描 pip 安装路径和常见解压路径。
#
# 手动指定:
#   cmake .. -DOpenVINO_DIR=~/.local/lib/python3.x/site-packages/openvino/runtime/cmake
#   cmake .. -DOpenVINO_DIR=/opt/intel/openvino/runtime/cmake
#   cmake .. -DOpenVINO_DIR=/home/pi/openvino_runtime/runtime/cmake
# ============================================================
find_package(OpenVINO QUIET)
if(NOT OpenVINO_FOUND)
    # pip 安装路径（支持多 Python 版本）
    file(GLOB _pip_cmake_dirs
        "$ENV{HOME}/.local/lib/python3.*/site-packages/openvino/runtime/cmake")

    # tar.gz 解压到家目录
    file(GLOB _tgz_cmake_dirs "$ENV{HOME}/openvino*/runtime/cmake")

    set(_ov_candidates
        ${_pip_cmake_dirs}
        "/opt/intel/openvino/runtime/cmake"
        ${_tgz_cmake_dirs}
    )
    foreach(_dir ${_ov_candidates})
        if(EXISTS "${_dir}/OpenVINOConfig.cmake")
            set(OpenVINO_DIR "${_dir}")
            break()
        endif()
    endforeach()

    find_package(OpenVINO REQUIRED)
endif()
message(STATUS "[OpenVINO] dir: ${OpenVINO_DIR}")

# ============================================================
# MindVision 工业相机 SDK
#
# 移除硬编码路径，支持环境变量和自动搜索。
#
# 使用方式:
#   export MINDVISION_ROOT=/path/to/mvs_sdk
#   cmake ..
#
#   或:
#   cmake .. -DMINDVISION_ROOT=/path/to/mvs_sdk
#
#   或: 将 SDK 放在项目同级目录（自动发现）:
#   mvs_openvino_demo/../
#     linuxSDK_V2.1.0.xxx/
#       include/CameraApi.h
#       lib/x64/libMVSDK.so
# ============================================================
set(MINDVISION_ROOT "$ENV{MINDVISION_ROOT}"
    CACHE PATH "MindVision Linux SDK installation root")

# 自动搜索项目同级目录下的 linuxSDK*
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

message(STATUS "[OpenCV]  version: ${OpenCV_VERSION}")
message(STATUS "[MV_SDK] include: ${MINDVISION_INCLUDE_DIR}")
message(STATUS "[MV_SDK] library: ${MINDVISION_LIBRARY}")

# ============================================================
# 源文件
# ============================================================
set(ALL_SOURCES
    main.cpp
    src/camera.cpp
    src/config.cpp
    src/detector.cpp
    src/spatial.cpp
    src/visualize.cpp
    src/CRC16.cpp
    src/VirtualSerial.cpp
)
# 注: src/preprocess.cpp 已移除 — 预处理由 OpenVINO PrePostProcessor 在模型图中完成

add_executable(${PROJECT_NAME} ${ALL_SOURCES})

# ============================================================
# 头文件路径（target 级别，不污染全局 include）
# ============================================================
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${OpenCV_INCLUDE_DIRS}
    ${MINDVISION_INCLUDE_DIR}
)

# ============================================================
# 链接库
# ============================================================
target_link_libraries(${PROJECT_NAME} PRIVATE
    ${OpenCV_LIBS}
    openvino::runtime
    ${MINDVISION_LIBRARY}
    pthread
    dl
)

# ============================================================
# RPATH — 运行时自动找到 libMVSDK.so，无需 LD_LIBRARY_PATH
# ============================================================
get_filename_component(MINDVISION_LIB_DIR "${MINDVISION_LIBRARY}" DIRECTORY)
set_target_properties(${PROJECT_NAME} PROPERTIES
    BUILD_RPATH "${MINDVISION_LIB_DIR}"
)

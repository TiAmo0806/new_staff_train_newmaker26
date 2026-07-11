cmake_minimum_required(VERSION 3.16)
project(bean_vision LANGUAGES CXX)

# ============================================================
#  1. C++ 标准
# ============================================================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================
#  2. 寻找依赖
# ============================================================

# ---- OpenCV ----
find_package(OpenCV REQUIRED)
message(STATUS "OpenCV: ${OpenCV_VERSION}")

# ---- yaml-cpp ----
find_package(yaml-cpp QUIET)
if(NOT yaml-cpp_FOUND)
    # 尝试 pkg-config
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(YAMLCPP yaml-cpp)
    endif()
    if(NOT YAMLCPP_FOUND)
        message(FATAL_ERROR "❌ 找不到 yaml-cpp，请安装: sudo apt install libyaml-cpp-dev")
    endif()
endif()

# ---- ONNX Runtime (项目内) ----
set(ONNXRUNTIME_ROOT "${CMAKE_SOURCE_DIR}/include/onnxruntime-sdk")
set(ONNXRUNTIME_INCLUDE "${ONNXRUNTIME_ROOT}/include")
set(ONNXRUNTIME_LIB     "${ONNXRUNTIME_ROOT}/lib/libonnxruntime.so")

if(NOT EXISTS "${ONNXRUNTIME_LIB}")
    message(FATAL_ERROR "❌ 找不到 ONNX Runtime 库: ${ONNXRUNTIME_LIB}")
endif()

# ---- MindVision SDK ----
# 如果 SDK 不在默认路径，通过 -DMVSDK_ROOT=/path/to/sdk 指定
if(NOT DEFINED MVSDK_ROOT)
    set(MVSDK_ROOT "/opt/MVS" CACHE PATH "MindVision SDK 根目录")
endif()
set(MVSDK_INCLUDE "${MVSDK_ROOT}/include")
set(MVSDK_LIB     "${MVSDK_ROOT}/lib/libMVSDK.so")

# 尝试自动查找
if(NOT EXISTS "${MVSDK_LIB}")
    find_library(MVSDK_LIB_FOUND NAMES MVSDK libMVSDK
                 PATHS /opt/MVS/lib /usr/lib /usr/local/lib
                       /opt/MindVision/lib)
    if(MVSDK_LIB_FOUND)
        set(MVSDK_LIB "${MVSDK_LIB_FOUND}")
    endif()
endif()
# 找头文件
if(NOT EXISTS "${MVSDK_INCLUDE}/CameraApi.h")
    find_path(MVSDK_INCLUDE_FOUND CameraApi.h
              PATHS /opt/MVS/include /usr/include /usr/local/include
                    /opt/MindVision/include
              PATH_SUFFIXES MVCAMSDK MVSDK mindvision)
    if(MVSDK_INCLUDE_FOUND)
        set(MVSDK_INCLUDE "${MVSDK_INCLUDE_FOUND}")
    endif()
endif()

# ============================================================
#  3. 源文件
# ============================================================
set(SOURCES
    src/vision.cpp
    src/config.cpp
    src/serial.cpp
    src/camera.cpp
    src/datection.cpp
    src/CRC16.cpp
    src/visualizer.cpp
    src/stable_tracker.cpp
)

# ============================================================
#  4. 可执行文件
# ============================================================
add_executable(${PROJECT_NAME} ${SOURCES})

# ============================================================
#  5. 头文件路径
# ============================================================
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${ONNXRUNTIME_INCLUDE}
)

# 相机 SDK 头文件（如果存在）
if(EXISTS "${MVSDK_INCLUDE}/CameraApi.h")
    target_include_directories(${PROJECT_NAME} PRIVATE "${MVSDK_INCLUDE}")
    message(STATUS "MindVision SDK: ${MVSDK_INCLUDE}")
else()
    message(WARNING "⚠️ 未找到 MindVision SDK 头文件 (CameraApi.h)，请设置 -DMVSDK_ROOT=...")
endif()

# ============================================================
#  6. 链接
# ============================================================
target_link_libraries(${PROJECT_NAME} PRIVATE
    ${OpenCV_LIBS}
    ${ONNXRUNTIME_LIB}
)

# yaml-cpp
if(yaml-cpp_FOUND)
    target_link_libraries(${PROJECT_NAME} PRIVATE yaml-cpp)
else()
    target_link_libraries(${PROJECT_NAME} PRIVATE ${YAMLCPP_LIBRARIES})
    target_include_directories(${PROJECT_NAME} PRIVATE ${YAMLCPP_INCLUDE_DIRS})
endif()

# MindVision SDK
if(EXISTS "${MVSDK_LIB}")
    target_link_libraries(${PROJECT_NAME} PRIVATE "${MVSDK_LIB}")
    message(STATUS "MindVision SDK lib: ${MVSDK_LIB}")
else()
    message(WARNING "⚠️ 未找到 libMVSDK.so，链接时将缺失相机功能")
endif()

# 系统库
target_link_libraries(${PROJECT_NAME} PRIVATE pthread)

# ============================================================
#  7. 编译选项
# ============================================================
target_compile_options(${PROJECT_NAME} PRIVATE
    -Wall -Wextra -O2
)

# ONNX Runtime 运行时路径（若使用 rpath，模型运行时找得到 .so）
set_target_properties(${PROJECT_NAME} PROPERTIES
    BUILD_RPATH "${ONNXRUNTIME_ROOT}/lib"
    INSTALL_RPATH "${ONNXRUNTIME_ROOT}/lib"
)

# ============================================================
#  8. 打印配置摘要
# ============================================================
message(STATUS "========================================")
message(STATUS "  bean_vision 构建配置")
message(STATUS "========================================")
message(STATUS "  C++ 标准:     ${CMAKE_CXX_STANDARD}")
message(STATUS "  OpenCV:        ${OpenCV_VERSION}")
message(STATUS "  ONNX Runtime:  ${ONNXRUNTIME_LIB}")
message(STATUS "  MVSDK include: ${MVSDK_INCLUDE}")
message(STATUS "  MVSDK lib:     ${MVSDK_LIB}")
message(STATUS "========================================")

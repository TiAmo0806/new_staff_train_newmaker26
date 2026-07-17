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

# ---- OpenVINO ----
find_package(OpenVINO REQUIRED)
message(STATUS "OpenVINO: ${OpenVINO_VERSION}")

# ---- MindVision SDK ----
# 优先级：项目内置 > -DMVSDK_ROOT=... > 系统默认路径
set(MVSDK_LOCAL "${CMAKE_SOURCE_DIR}/include/MVSDK")

if(NOT DEFINED MVSDK_ROOT)
    # 优先使用项目内置的 SDK
    if(EXISTS "${MVSDK_LOCAL}/include/CameraApi.h" AND EXISTS "${MVSDK_LOCAL}/lib/libMVSDK.so")
        set(MVSDK_ROOT "${MVSDK_LOCAL}" CACHE PATH "MindVision SDK 根目录")
        set(MVSDK_INCLUDE "${MVSDK_ROOT}/include")
        set(MVSDK_LIB     "${MVSDK_ROOT}/lib/libMVSDK.so")
        message(STATUS "MindVision SDK: 使用项目内置版本 (${MVSDK_LOCAL})")
    else()
        set(MVSDK_ROOT "/opt/MVS" CACHE PATH "MindVision SDK 根目录")
        set(MVSDK_INCLUDE "${MVSDK_ROOT}/include")
        set(MVSDK_LIB     "${MVSDK_ROOT}/lib/libMVSDK.so")
    endif()
else()
    set(MVSDK_INCLUDE "${MVSDK_ROOT}/include")
    set(MVSDK_LIB     "${MVSDK_ROOT}/lib/libMVSDK.so")
endif()

# 自动查找（兜底）
if(NOT EXISTS "${MVSDK_LIB}")
    find_library(MVSDK_LIB_FOUND NAMES MVSDK libMVSDK
                 PATHS ${MVSDK_LOCAL}/lib /opt/MVS/lib /usr/lib /usr/local/lib
                       /opt/MindVision/lib /opt/MV-SDK/lib/x64)
    if(MVSDK_LIB_FOUND)
        set(MVSDK_LIB "${MVSDK_LIB_FOUND}")
    endif()
endif()
# 找头文件
if(NOT EXISTS "${MVSDK_INCLUDE}/CameraApi.h")
    find_path(MVSDK_INCLUDE_FOUND CameraApi.h
              PATHS ${MVSDK_LOCAL}/include /opt/MVS/include /usr/include /usr/local/include
                    /opt/MindVision/include /opt/MV-SDK/include
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
    ${OpenVINO_INCLUDE_DIRS}
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
    openvino::runtime
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

# OpenVINO + MindVision SDK 运行时路径
set_target_properties(${PROJECT_NAME} PROPERTIES
    BUILD_RPATH "${MVSDK_ROOT}/lib"
    INSTALL_RPATH "${MVSDK_ROOT}/lib"
)

# ============================================================
#  8. 打印配置摘要
# ============================================================
message(STATUS "========================================")
message(STATUS "  bean_vision 构建配置")
message(STATUS "========================================")
message(STATUS "  C++ 标准:     ${CMAKE_CXX_STANDARD}")
message(STATUS "  OpenCV:        ${OpenCV_VERSION}")
message(STATUS "  OpenVINO:      ${OpenVINO_VERSION}")
message(STATUS "  MVSDK include: ${MVSDK_INCLUDE}")
message(STATUS "  MVSDK lib:     ${MVSDK_LIB}")
message(STATUS "========================================")

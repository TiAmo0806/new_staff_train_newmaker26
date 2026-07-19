# ============================================================
# CMakeLists.txt — Robot Vision 构建配置
#
# 构建两个目标:
#   robot_vision        完整比赛程序（状态机 + 串口 + 视觉）
#   test_recognition    独立识别测试程序（只做推理和显示）
#
# 依赖:
#   - OpenCV 4.5+（图像处理、显示、NMS）
#   - ONNX Runtime 1.23.2（模型推理）
#   - MindVision SDK（迈德威视工业相机驱动）
#
# 常见问题:
#   1. MindVision SDK 不在标准路径时，设环境变量:
#      export MINDVISION_SDK=/path/to/sdk
#   2. ONNX Runtime 库默认使用项目目录下的 libonnxruntime.so.1.23.2
#      如果找不到会回退到系统安装版本
# ============================================================

cmake_minimum_required(VERSION 3.12)
project(RobotVision)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(OpenCV REQUIRED)

# ⭐ 迈德威视 SDK 路径自动检测
# 优先使用环境变量 MINDVISION_SDK，否则自动搜索常见路径
if(DEFINED ENV{MINDVISION_SDK})
    set(MINDVISION_SDK_DIR "$ENV{MINDVISION_SDK}")
else()
    # 自动检测：优先搜索项目自带的 SDK，再找常见安装路径
    file(GLOB MINDVISION_CANDIDATES
        "${CMAKE_SOURCE_DIR}/third_party/mindvision"
        "/home/*/桌面/linuxSDK*"
        "/opt/mindvision*"
        "/usr/local/mindvision*"
    )
    list(LENGTH MINDVISION_CANDIDATES MINDVISION_COUNT)
    if(MINDVISION_COUNT GREATER 0)
        list(GET MINDVISION_CANDIDATES 0 MINDVISION_SDK_DIR)
        message(STATUS "Auto-detected MindVision SDK: ${MINDVISION_SDK_DIR}")
    else()
        message(WARNING "MindVision SDK not found! Set MINDVISION_SDK environment variable.")
    endif()
endif()

include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${OpenCV_INCLUDE_DIRS}
    ${MINDVISION_SDK_DIR}/include
    ${ONNXRUNTIME_INCLUDE_DIR}
    /usr/local/include
)

link_directories(
    ${MINDVISION_SDK_DIR}/lib/arm64
    /usr/lib/aarch64-linux-gnu
)

# ⭐ ONNX Runtime — 强制使用项目自带的库和头文件
set(ONNXRUNTIME_ROOT "${CMAKE_SOURCE_DIR}")
find_library(ONNXRUNTIME_LIB onnxruntime HINTS ${ONNXRUNTIME_ROOT} NO_DEFAULT_PATH)
if(NOT ONNXRUNTIME_LIB)
    message(FATAL_ERROR "Project-local libonnxruntime.so not found at ${ONNXRUNTIME_ROOT}! "
                        "Make sure libonnxruntime.so.1.23.2 and symlinks exist.")
endif()
message(STATUS "Using ONNX Runtime .so: ${ONNXRUNTIME_LIB}")

# 强制使用项目自带头文件（添加 BEFORE，确保优先于任何系统路径）
set(ONNXRUNTIME_INCLUDE_DIR "${ONNXRUNTIME_ROOT}/third_party/onnxruntime/include")
if(NOT EXISTS "${ONNXRUNTIME_INCLUDE_DIR}/onnxruntime_c_api.h")
    message(FATAL_ERROR "ONNX Runtime headers not found at ${ONNXRUNTIME_INCLUDE_DIR}!")
endif()
message(STATUS "Using ONNX Runtime headers: ${ONNXRUNTIME_INCLUDE_DIR}")

# ⭐ 嵌入 rpath，使运行时能在项目目录下找到 libonnxruntime.so
# 这样就无需设置 LD_LIBRARY_PATH（不过 run.sh 中仍然设置了以保兼容）
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-rpath,${CMAKE_SOURCE_DIR}")

# ============================================================
# 目标 1：robot_vision（完整比赛程序）
# ============================================================

add_executable(robot_vision
    src/main.cpp
    src/RobotVision.cpp
    src/VisionController.cpp
    src/Visualization.cpp
    src/SerialPort.cpp
    src/MindVisionCamera.cpp
)

target_include_directories(robot_vision BEFORE PRIVATE ${ONNXRUNTIME_INCLUDE_DIR})
target_link_libraries(robot_vision
    ${OpenCV_LIBS}
    pthread
    MVSDK           # 迈德威视相机 SDK
    ${ONNXRUNTIME_LIB}
)

# ============================================================
# 目标 2：test_recognition（独立识别测试程序）
# ============================================================

add_executable(test_recognition
    src/test_recognition.cpp
    src/RobotVision.cpp
    src/Visualization.cpp
    src/MindVisionCamera.cpp
)

target_include_directories(test_recognition BEFORE PRIVATE ${ONNXRUNTIME_INCLUDE_DIR})
target_link_libraries(test_recognition
    ${OpenCV_LIBS}
    pthread
    MVSDK
    ${ONNXRUNTIME_LIB}
)

# 设置输出目录到 bin/
set_target_properties(robot_vision PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
)

set_target_properties(test_recognition PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
)

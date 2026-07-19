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

cmake_minimum_required(VERSION 3.10)
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
    # 自动检测：搜索常见路径（桌面、/opt、/usr/local）
    file(GLOB MINDVISION_CANDIDATES
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
    /usr/local/include
)

link_directories(
    ${MINDVISION_SDK_DIR}/lib/x64
    /usr/lib/x86_64-linux-gnu
)

# ⭐ ONNX Runtime 库查找
# 优先使用项目目录下的新版 libonnxruntime.so.1.23.2
# （系统安装的 1.15.1 版不支持模型 IR v10）
find_library(ONNXRUNTIME_LIB onnxruntime HINTS ${CMAKE_SOURCE_DIR} NO_DEFAULT_PATH)
if(NOT ONNXRUNTIME_LIB)
    message(WARNING "Project-local libonnxruntime not found, falling back to system")
    set(ONNXRUNTIME_LIB onnxruntime)
endif()
message(STATUS "Using ONNX Runtime: ${ONNXRUNTIME_LIB}")

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

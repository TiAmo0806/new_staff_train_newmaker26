cmake_minimum_required(VERSION 3.10)
project(bean_sorter VERSION 2.0 LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 迈德威视 SDK (项目自带)
set(MVSDK_PATH "${CMAKE_SOURCE_DIR}/SDK")

# ONNX Runtime
set(ONNX_PATH "/home/wzy/onnxruntime-linux-x64-1.20.1")

# 运行时库搜索路径
set(CMAKE_BUILD_RPATH "${MVSDK_PATH}/lib;${ONNX_PATH}/lib")

# OpenCV
find_package(OpenCV REQUIRED)
include_directories(${OpenCV_INCLUDE_DIRS})

# 通用源文件
set(COMMON_SRC
  Communication/CRC16.cpp
  Communication/ByteConverter.cpp
  src/BeanProtocol.cpp
  src/SerialPort.cpp
)

# ========== Vision Node ==========
add_executable(VisionNode
 Communication/VisionNode.cpp
  CameraDriver/CameraDriver.cpp
  ${COMMON_SRC}
)

target_include_directories(VisionNode PRIVATE
  src
  Communication
  CameraDriver
  ${MVSDK_PATH}/include
  ${ONNX_PATH}/include
)

target_link_directories(VisionNode PRIVATE
  ${MVSDK_PATH}/lib
  ${ONNX_PATH}/lib
)

target_link_libraries(VisionNode
  ${OpenCV_LIBS}
  MVSDK
  onnxruntime
)


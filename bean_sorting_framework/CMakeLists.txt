 cmake_minimum_required(VERSION 3.10)
  project(BeanSortingFramework VERSION 1.0 LANGUAGES CXX)

  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)

  find_package(OpenCV REQUIRED)
  find_package(yaml-cpp REQUIRED)

  add_definitions(-DHAS_MV_CAMERA)

  include_directories(${CMAKE_CURRENT_SOURCE_DIR})
  include_directories(${CMAKE_CURRENT_SOURCE_DIR}/SDK/include)
  include_directories(${OpenCV_INCLUDE_DIRS})
  include_directories(/usr/local/include)

  set(MAIN_SOURCES
      main.cpp
      Communication/CRC16.cpp
      Communication/ByteConverter.cpp
      Communication/BeanSerial.cpp
      Detector/BeanDetector.cpp
      CameraDriver/MvCamera.cpp
  )

  add_executable(bean_sorting ${MAIN_SOURCES})
  target_link_directories(bean_sorting PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/SDK/lib/x64)
  target_link_libraries(bean_sorting PRIVATE pthread ${OpenCV_LIBS} yaml-cpp MVSDK onnxruntime)
cmake_minimum_required(VERSION 3.16)

project(bean_vision_framework LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenCV REQUIRED)

set(ONNXRUNTIME_ROOT "${CMAKE_SOURCE_DIR}/bisai/onnxruntime-linux-x64-1.27.0"
    CACHE PATH "ONNXRuntime root directory")
find_library(ONNXRUNTIME_LIB onnxruntime
    PATHS "${ONNXRUNTIME_ROOT}/lib"
    REQUIRED)

option(WITH_MINDVISION "Enable MindVision industrial camera support" ON)
set(MINDVISION_ROOT "" CACHE PATH "MindVision Linux SDK root directory")

set(BVP_MINDVISION_AVAILABLE OFF)

if(WITH_MINDVISION AND NOT MINDVISION_ROOT)
    foreach(candidate IN ITEMS
        "${CMAKE_SOURCE_DIR}/camera/linuxSDK_V2.1.0.49202602041120"
        "${CMAKE_SOURCE_DIR}/camera"
        "${CMAKE_SOURCE_DIR}/../linuxSDK_V2.1.0.49202602041120"
        "${CMAKE_SOURCE_DIR}/../camera/linuxSDK_V2.1.0.49202602041120"
        "${CMAKE_SOURCE_DIR}/third_party/camera/linuxSDK_V2.1.0.49202602041120"
        "${CMAKE_SOURCE_DIR}/third_party/camera"
        "${CMAKE_SOURCE_DIR}/sdk/linuxSDK_V2.1.0.49202602041120"
        "${CMAKE_SOURCE_DIR}/sdk"
    )
        if(EXISTS "${candidate}/include/CameraApi.h")
            set(MINDVISION_ROOT "${candidate}" CACHE PATH "MindVision Linux SDK root directory" FORCE)
            break()
        endif()
    endforeach()
endif()

if(WITH_MINDVISION AND EXISTS "${MINDVISION_ROOT}/include/CameraApi.h")
    set(BVP_MINDVISION_AVAILABLE ON)
endif()

include_directories(include)
include_directories(${ONNXRUNTIME_ROOT}/include)
include_directories(${OpenCV_INCLUDE_DIRS})

if(BVP_MINDVISION_AVAILABLE)
    set(MINDVISION_INCLUDE_DIR "${MINDVISION_ROOT}/include"
        CACHE PATH "MindVision CameraApi.h include directory")
    find_library(MINDVISION_LIB MVSDK
        PATHS "${MINDVISION_ROOT}/lib/x64"
        REQUIRED)
    include_directories(${MINDVISION_INCLUDE_DIR})
else()
    message(WARNING
        "MindVision SDK not found. Camera-related targets will be built without industrial camera support. "
        "Set -DMINDVISION_ROOT=/path/to/linuxSDK_V2.x.x to enable it.")
endif()

file(GLOB_RECURSE PROJECT_SOURCES CONFIGURE_DEPENDS
    src/*.cpp
)

add_executable(bean_vision_framework ${PROJECT_SOURCES})
add_executable(camera_preview_demo
    tools/camera_preview_demo/main.cpp
    src/camera/CameraManager.cpp
    src/core/AppConfig.cpp
    src/core/DetectionTypes.cpp
    src/detector/BeanNumberDetector.cpp
    src/input/InputManager.cpp
    src/utils/DrawUtils.cpp
)

set(BVP_PROTOCOL_DEMO_SOURCES
    tools/serial_debug_demo/main.cpp
    src/communication/ByteConverter.cpp
    src/communication/CRC16.cpp
    src/communication/Protocol.cpp
    src/communication/SerialPort.cpp
)

add_executable(serial_protocol_demo ${BVP_PROTOCOL_DEMO_SOURCES})
add_executable(serial_debug_demo ${BVP_PROTOCOL_DEMO_SOURCES})

foreach(target_name IN ITEMS bean_vision_framework camera_preview_demo)
    target_link_libraries(${target_name} PRIVATE ${OpenCV_LIBS})
endforeach()

target_link_libraries(bean_vision_framework PRIVATE ${ONNXRUNTIME_LIB})
target_link_libraries(camera_preview_demo PRIVATE ${ONNXRUNTIME_LIB})

if(BVP_MINDVISION_AVAILABLE)
    foreach(target_name IN ITEMS bean_vision_framework camera_preview_demo)
        target_compile_definitions(${target_name} PRIVATE BVP_WITH_MINDVISION)
        target_link_libraries(${target_name} PRIVATE ${MINDVISION_LIB})
    endforeach()
endif()

foreach(target_name IN ITEMS serial_protocol_demo serial_debug_demo)
    target_link_libraries(${target_name} PRIVATE ${OpenCV_LIBS})
endforeach()

foreach(target_name IN ITEMS bean_vision_framework camera_preview_demo serial_protocol_demo serial_debug_demo)
    set_target_properties(${target_name} PROPERTIES
        INSTALL_RPATH "$ORIGIN:$ORIGIN/../bisai/onnxruntime-linux-x64-1.27.0/lib"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
endforeach()

get_filename_component(ONNXRUNTIME_LIB_DIR "${ONNXRUNTIME_LIB}" DIRECTORY)
file(GLOB ONNXRUNTIME_SO_FILES "${ONNXRUNTIME_LIB_DIR}/libonnxruntime.so*")

foreach(target_name IN ITEMS bean_vision_framework camera_preview_demo serial_protocol_demo serial_debug_demo)
    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ONNXRUNTIME_SO_FILES}
            $<TARGET_FILE_DIR:${target_name}>
    )
endforeach()

if(BVP_MINDVISION_AVAILABLE)
    foreach(target_name IN ITEMS bean_vision_framework camera_preview_demo)
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${MINDVISION_LIB}
                $<TARGET_FILE_DIR:${target_name}>
        )
    endforeach()
endif()

foreach(target_name IN ITEMS bean_vision_framework camera_preview_demo serial_protocol_demo serial_debug_demo)
    target_compile_options(${target_name} PRIVATE -Wall -Wextra -pedantic)
endforeach()

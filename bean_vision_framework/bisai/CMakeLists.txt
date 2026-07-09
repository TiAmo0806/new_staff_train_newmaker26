cmake_minimum_required(VERSION 3.14)
project(BeanNumberDetector VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenCV REQUIRED COMPONENTS core videoio imgproc imgcodecs dnn)

set(ONNXRUNTIME_ROOT "${CMAKE_SOURCE_DIR}/onnxruntime-sdk")
set(ONNXRUNTIME_INCLUDE_DIRS "${ONNXRUNTIME_ROOT}/include")
set(ONNXRUNTIME_LIBRARIES "${ONNXRUNTIME_ROOT}/lib/libonnxruntime.so")

if(NOT EXISTS "${ONNXRUNTIME_INCLUDE_DIRS}/onnxruntime_cxx_api.h")
    message(FATAL_ERROR
        "Missing ONNX Runtime headers at ${ONNXRUNTIME_INCLUDE_DIRS}. "
        "Restore the onnxruntime-sdk directory first.")
endif()

if(NOT EXISTS "${ONNXRUNTIME_LIBRARIES}")
    message(FATAL_ERROR
        "Missing ONNX Runtime library at ${ONNXRUNTIME_LIBRARIES}. "
        "Restore the onnxruntime-sdk directory first.")
endif()

add_executable(bean_number_detector main.cpp)

target_link_libraries(bean_number_detector PRIVATE
    ${OpenCV_LIBS}
    ${ONNXRUNTIME_LIBRARIES}
)

target_include_directories(bean_number_detector PRIVATE
    ${OpenCV_INCLUDE_DIRS}
    ${ONNXRUNTIME_INCLUDE_DIRS}
)

set_target_properties(bean_number_detector PROPERTIES
    BUILD_RPATH "${ONNXRUNTIME_ROOT}/lib"
)

if(MSVC)
    target_compile_options(bean_number_detector PRIVATE /W4 /O2)
    target_compile_definitions(bean_number_detector PRIVATE NOMINMAX)
else()
    target_compile_options(bean_number_detector PRIVATE -Wall -Wextra -O3 -march=native)
endif()

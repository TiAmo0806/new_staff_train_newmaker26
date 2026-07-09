cmake_minimum_required(VERSION 3.14)
project(MDVS_demo)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 项目根目录
include_directories(${PROJECT_SOURCE_DIR})

# OpenCV
find_package(OpenCV REQUIRED)
include_directories(${OpenCV_INCLUDE_DIRS})

# 迈德威视SDK 真实路径（和你截图完全匹配）
set(MV_SDK_ROOT "/home/hu/mvs/linuxSDK_V2.1.0.49202602041120")
include_directories(${MV_SDK_ROOT}/include)
link_directories(${MV_SDK_ROOT}/lib/x64)

# 编译程序
add_executable(MDVS_demo main.cpp)

# 链接库：库名改成 MVSDK，不是MVApi
target_link_libraries(MDVS_demo
    ${OpenCV_LIBS}
    MVSDK
    pthread
    dl
)
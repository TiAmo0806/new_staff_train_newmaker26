cmake_minimum_required(VERSION 3.14)
project(mvs_openvino_demo)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================
# OpenCV
# ============================================================
find_package(OpenCV REQUIRED)
include_directories(${OpenCV_INCLUDE_DIRS})

# ============================================================
# OpenVINO（pip 安装，直接指定路径）
# ============================================================
set(OPENVINO_ROOT "$ENV{HOME}/.local/lib/python3.10/site-packages/openvino")

# 头文件目录
include_directories(${OPENVINO_ROOT}/include)

# 库文件目录
# pip 安装的 .so 文件带版本后缀（如 libopenvino.so.2621），
# 创建不带版本号的符号链接，使 -lopenvino 能被 ld 找到
set(OPENVINO_LIB_DIR ${OPENVINO_ROOT}/libs)
set(OPENVINO_LINK_DIR ${CMAKE_BINARY_DIR}/openvino_links)

# 在 build 目录下创建符号链接
file(MAKE_DIRECTORY ${OPENVINO_LINK_DIR})
file(GLOB OPENVINO_SO_FILES "${OPENVINO_LIB_DIR}/libopenvino.so.*")
if(OPENVINO_SO_FILES)
    list(GET OPENVINO_SO_FILES 0 OPENVINO_SO_FILE)
    get_filename_component(OPENVINO_SO_NAME ${OPENVINO_SO_FILE} NAME_WE)
    # libopenvino.so.2621 → libopenvino.so
    execute_process(COMMAND ln -sf ${OPENVINO_SO_FILE} ${OPENVINO_LINK_DIR}/libopenvino.so)
    message(STATUS "[OpenVINO] 创建符号链接: libopenvino.so -> ${OPENVINO_SO_NAME}")
endif()

# 也对 libopenvino_c 和 frontend 库创建链接
file(GLOB OPENVINO_C_SO "${OPENVINO_LIB_DIR}/libopenvino_c.so.*")
if(OPENVINO_C_SO)
    list(GET OPENVINO_C_SO 0 OV_C_FILE)
    execute_process(COMMAND ln -sf ${OV_C_FILE} ${OPENVINO_LINK_DIR}/libopenvino_c.so)
endif()

# 运行时需要的库：libopenvino.so.2621, libopenvino_c.so.2621, libopenvino_ir_frontend.so.2621, libtbb.so.12, libhwloc.so.15
link_directories(${OPENVINO_LINK_DIR} ${OPENVINO_LIB_DIR})

message(STATUS "[OpenVINO] root:    ${OPENVINO_ROOT}")
message(STATUS "[OpenVINO] include: ${OPENVINO_ROOT}/include")
message(STATUS "[OpenVINO] lib:     ${OPENVINO_LIB_DIR}")

# ============================================================
# 迈德威视 SDK
# ============================================================
set(MV_SDK_ROOT "/home/hu/mvs/linuxSDK_V2.1.0.49202602041120"
    CACHE PATH "MindVision SDK root directory")
include_directories(${MV_SDK_ROOT}/include)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(MV_LIB_DIR ${MV_SDK_ROOT}/lib/x64)
else()
    set(MV_LIB_DIR ${MV_SDK_ROOT}/lib/x86)
endif()
link_directories(${MV_LIB_DIR})

message(STATUS "[MV_SDK]  root: ${MV_SDK_ROOT}")
message(STATUS "[MV_SDK]  lib:  ${MV_LIB_DIR}")

# ============================================================
# 可执行文件（各模块的 .cpp 源文件）
# ============================================================
add_executable(${PROJECT_NAME}
    main.cpp
    src/camera.cpp
    src/detector.cpp
    src/preprocess.cpp
    src/visualize.cpp
)

# ============================================================
# 链接库
# ============================================================
target_link_libraries(${PROJECT_NAME} PRIVATE
    ${OpenCV_LIBS}
    openvino
    MVSDK
    pthread
    dl
)

# ============================================================
# 将模型文件复制到 build 目录
# 使用方式：先手动将 best.xml 和 best.bin 放到项目目录下，然后取消下面的注释
#   或者直接将模型文件复制到 build 目录，运行时用参数指定路径
# ============================================================
# add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
#     COMMAND ${CMAKE_COMMAND} -E copy_if_different
#         ${PROJECT_SOURCE_DIR}/best.xml
#         $<TARGET_FILE_DIR:${PROJECT_NAME}>/best.xml
#     COMMAND ${CMAKE_COMMAND} -E copy_if_different
#         ${PROJECT_SOURCE_DIR}/best.bin
#         $<TARGET_FILE_DIR:${PROJECT_NAME}>/best.bin
#     COMMENT "Copying model files to build directory..."
# )

make_minimum_required(VERSION 3.10)

# ============================================================================
# Konggu 采集客户端 (热成像机芯 + 可见光摄像头) —— CMake 模板
# ----------------------------------------------------------------------------
# 使用：
#   cmake -S . -B build
#   cmake --build build -j
#
#   私有 SDK 库默认在 ./lib；若放在别处：
#   cmake -S . -B build -DKONGGU_SDK_DIR=/path/to/sdk
#
# 交叉编译（嵌入式 ARM / RKNN 平台，按需取消注释并改成你自己的工具链）：
#   set(CMAKE_SYSTEM_NAME Linux)
#   set(CMAKE_C_COMPILER /path/to/aarch64-linux-gnu-gcc)
#   set(CMAKE_CXX_COMPILER /path/to/aarch64-linux-gnu-g++)
#
# 自动查找的系统依赖：OpenCV / CURL / Threads / nlohmann-json
# 供应商私有依赖（需随项目分发或放在 KONGGU_SDK_DIR/lib）：
#   UThermalLib, unitcam, usb-1.0, Thermaltool, image_algorithms_lite, rknn_api
# ============================================================================

project(KongguClient CXX C)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ----------------------------------------------------------------------------
# 0) 可选：交叉编译工具链（取消注释并填写真实路径）
# ----------------------------------------------------------------------------
# set(CMAKE_SYSTEM_NAME Linux)
# set(CMAKE_SYSTEM_PROCESSOR aarch64)
# set(CMAKE_C_COMPILER   /home/user/toolchain/bin/aarch64-linux-gnu-gcc)
# set(CMAKE_CXX_COMPILER /home/user/toolchain/bin/aarch64-linux-gnu-g++)
# set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
# set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
# set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# ----------------------------------------------------------------------------
# 1) 供应商 SDK 目录（默认 ./lib, ./include；可被 -DKONGGU_SDK_DIR=... 覆盖）
# ----------------------------------------------------------------------------
if(NOT DEFINED KONGGU_SDK_DIR)
    set(KONGGU_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
endif()

set(KONGGU_SDK_INCLUDE_DIR "${KONGGU_SDK_DIR}/include")
set(KONGGU_SDK_LIB_DIR     "${KONGGU_SDK_DIR}/lib")

# 提示性校验：UThermalLib 头文件是否存在
if(NOT EXISTS "${KONGGU_SDK_INCLUDE_DIR}/UThermalLib.h")
    message(WARNING "在 ${KONGGU_SDK_INCLUDE_DIR} 未找到 UThermalLib.h，"
                    "请用 -DKONGGU_SDK_DIR=... 指向 SDK 根目录")
endif()

# ----------------------------------------------------------------------------
# 2) 自动查找系统依赖
# ----------------------------------------------------------------------------
# OpenCV（可见光取流 / imwrite / Mat）
find_package(OpenCV REQUIRED)

# libcurl（HTTP 上传 / /clean）
find_package(CURL REQUIRED)

# pthread（上传线程）
find_package(Threads REQUIRED)

# nlohmann/json（解析服务端 JSON 响应）——优先找安装包，找不到再退回头文件搜索
if(NOT TARGET nlohmann_json::nlohmann_json)
    find_package(nlohmann_json QUIET)
endif()
if(NOT TARGET nlohmann_json::nlohmann_json)
    find_path(NLOHMANN_JSON_INCLUDE_DIR NAMES nlohmann/json.hpp)
    if(NOT NLOHMANN_JSON_INCLUDE_DIR)
        message(FATAL_ERROR "未找到 nlohmann/json.hpp：请安装 nlohmann-json3-dev，"
                            "或把 json 头文件放入 ${KONGGU_SDK_INCLUDE_DIR}/nlohmann/")
    endif()
endif()

# ----------------------------------------------------------------------------
# 3) 可执行目标
# ----------------------------------------------------------------------------
add_executable(konggu_client main.cpp)

target_include_directories(konggu_client PRIVATE
    ${KONGGU_SDK_INCLUDE_DIR}
    ${OpenCV_INCLUDE_DIRS}
    ${CURL_INCLUDE_DIRS}
    ${NLOHMANN_JSON_INCLUDE_DIR}
)

# 私有 SDK 库（按名查找；找不到给出可读错误）
foreach(_lib UThermalLib unitcam usb-1.0 Thermaltool image_algorithms_lite rknn_api)
    find_library(${_lib}_LIBRARY NAMES ${_lib} PATHS "${KONGGU_SDK_LIB_DIR}" NO_DEFAULT_PATH)
    if(NOT ${_lib}_LIBRARY)
        message(WARNING "私有库 lib${_lib}.so 未在 ${KONGGU_SDK_LIB_DIR} 中找到，"
                        "链接可能失败。请确认 SDK lib 目录完整。")
    endif()
endforeach()

target_link_libraries(konggu_client PRIVATE
    ${UThermalLib_LIBRARY}
    ${unitcam_LIBRARY}
    ${usb-1.0_LIBRARY}
    ${Thermaltool_LIBRARY}
    ${image_algorithms_lite_LIBRARY}
    ${rknn_api_LIBRARY}
    ${CMAKE_THREAD_LIBS_INIT}
    ${OpenCV_LIBS}
    ${CURL_LIBRARIES}
)

# 运行时把 SDK .so 目录写入 RPATH，便于直接在 build/ 下执行 ./konggu_client
set_target_properties(konggu_client PROPERTIES
    BUILD_RPATH "${KONGGU_SDK_LIB_DIR}"
    INSTALL_RPATH "${KONGGU_SDK_LIB_DIR}"
)

# ----------------------------------------------------------------------------
# 4) （可选）可见光摄像头自检工具：TestCamera/camera_main.cpp
# ----------------------------------------------------------------------------
option(KONGGU_BUILD_CAMERA_TEST "构建摄像头自检工具 camera_test" ON)
if(KONGGU_BUILD_CAMERA_TEST AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/camera_main.cpp")
    add_executable(camera_test tools/camera_main.cpp)
    target_link_libraries(camera_test PRIVATE ${OpenCV_LIBS})
endif()

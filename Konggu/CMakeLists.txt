cmake_minimum_required(VERSION 3.10)

# 项目名称和语言
project(MainProject CXX C)

message(STATUS "C++ version: ${CMAKE_CXX_STANDARD}")

# 设置交叉编译工具链路径（按需启用）
# set(CMAKE_SYSTEM_NAME Linux)
# set(CMAKE_C_COMPILER /home/wmx/jx003_lib/toolchain/gcc-linaro-7.5.0-2019.12-x86_64_aarch64-linux-gnu/bin/aarch64-linux-gnu-gcc)
# set(CMAKE_CXX_COMPILER /home/wmx/jx003_lib/toolchain/gcc-linaro-7.5.0-2019.12-x86_64_aarch64-linux-gnu/bin/aarch64-linux-gnu-g++)
# set(CMAKE_C_COMPILER /home/wmx/UTi591A/tools/linux/toolchain/arm-rockchip830-linux-gnueabihf/bin/arm-linux-gnueabihf-gcc)
# set(CMAKE_CXX_COMPILER /home/wmx/UTi591A/tools/linux/toolchain/arm-rockchip830-linux-gnueabihf/bin/arm-linux-gnueabihf-g++)

# 本地编译器设置
set(CMAKE_C_COMPILER /usr/bin/gcc)
set(CMAKE_CXX_COMPILER /usr/bin/g++)

# 指定库的路径
link_directories(./lib)

# 包含头文件目录
include_directories(./include)

# 查找OpenCV库
find_package(OpenCV REQUIRED)

# 查找CURL库
find_package(CURL REQUIRED)

# 添加可执行文件
add_executable(main main.cpp)

# 链接项目所需的库，包括OpenCV和curl
target_link_libraries(main UThermalLib pthread unitcam usb-1.0 Thermaltool image_algorithms_lite rknn_api ${OpenCV_LIBS} ${CURL_LIBRARIES})

# 包含OpenCV和CURL的头文件路径
target_include_directories(main PRIVATE ${OpenCV_INCLUDE_DIRS} ${CURL_INCLUDE_DIRS})

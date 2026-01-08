cmake_minimum_required(VERSION 3.10)
project(httpserver LANGUAGES CXX)

# 基础编译配置
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_compile_options(-Wall -Wextra -g)

# 编译产物输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# 头文件目录：优先项目内gtest头文件 + 项目自身头文件
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/gtest/include
)

# 优先链接项目内gtest静态库目录
link_directories(${CMAKE_CURRENT_SOURCE_DIR}/third_party/gtest/lib)

# 项目核心源文件（仅保留实际用到的，可根据实际增删）
set(SOCKET_SOURCES
    src/net/socket.cpp
    src/net/address.cpp
)

# 编译项目静态库
add_library(httpserver_net STATIC ${SOCKET_SOURCES})
find_package(Threads REQUIRED)
target_link_libraries(httpserver_net PRIVATE Threads::Threads)

# ========== GTest测试程序核心配置 ==========
# 1.检查测试文件是否存在
if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/test/third_party/test_gtest.cpp)
    message(FATAL_ERROR "测试文件不存在：test/third_party/test_gtest.cpp，请检查路径！")
endif()
# 2.编译测试程序
add_executable(test_gtest test/third_party/test_gtest.cpp)
# 3.链接依赖（优先项目内gtest）
target_link_libraries(test_gtest
    PRIVATE
    httpserver_net
    gtest
    gtest_main
    Threads::Threads
)


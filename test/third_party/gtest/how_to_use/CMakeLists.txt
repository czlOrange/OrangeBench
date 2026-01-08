cmake_minimum_required(VERSION 3.10)
project(CalculatorTests)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(GTest REQUIRED)
include_directories(.)

# 把Calculator的实现文件编译成库
add_library(calculator_lib calculator.cpp)

# 测试程序链接这个库
add_executable(calculator_tests test_calculator.cpp)
target_link_libraries(calculator_tests calculator_lib GTest::gtest GTest::gtest_main)

enable_testing()
add_test(NAME CalculatorTests COMMAND calculator_tests)
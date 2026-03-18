#!/bin/bash
# 编译脚本说明：
# 1. 必须编译 buffer.cpp 实现文件，否则报未定义引用
# 2. -std=c++11 是必须的，你的代码用了C++11特性+GTest依赖
# 3. -I./include 指定头文件路径
# 4. -g 带调试信息，-Wall 显示警告，-O2 编译优化
# 5. -lpthread 必须加，GTest依赖线程库

echo "开始编译 RingBuffer 测试程序（含实现文件）..."
g++ -std=c++14 -g -O2 -Wall \
src/net/buffer.cpp \
test/net/buffer.cpp \
-o test/net/buffer \
-I./include \
-lgtest -lgtest_main -lpthread

# 编译结果判断
if [ $? -eq 0 ]; then
    echo -e "\033[32m✅ 编译成功！生成可执行文件: ./buffer_test\033[0m"
    echo -e "\033[33m🔧 运行测试命令: ./buffer_test\033[0m"
else
    echo -e "\033[31m❌ 编译失败，请检查错误信息！\033[0m"
    exit 1
fi
#!/bin/bash

# ============================================================================
# RingBuffer 测试编译脚本
# ============================================================================

echo "========== 编译 RingBuffer 测试 =========="

# 编译命令
g++ -std=c++17 \
    -I../../../include \
    test_buffer.cpp \
    ../../../src/net/buffer.cpp \
    -lgtest -lgtest_main -lpthread \
    -o test_buffer

# 检查编译结果
if [ $? -eq 0 ]; then
    echo -e "\n✅ 编译成功！"
    echo "可执行文件: ./test_buffer"
    echo -e "\n========== 运行测试 =========="
    ./test_buffer
else
    echo -e "\n❌ 编译失败！"
    exit 1
fi
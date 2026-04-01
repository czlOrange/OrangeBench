#!/bin/bash
# test/core/run_memory_pool_test.sh

echo "开始编译和运行内存池测试套件..."
echo ""

echo "📦 编译测试程序..."
g++ -std=c++17 \
    -I../../../../../include \
    test_memory_pool.cpp \
    ../../../../../src/core/tcp_connections/Buffer/memory_pool.cpp \
    -lgtest -lgtest_main -lpthread \
    -o memory_pool_test

if [ $? -eq 0 ]; then
    echo "✅ 编译成功！"
    echo ""
    echo "🚀 开始运行测试..."
    echo ""
    
    ./memory_pool_test
    
    echo ""
    echo "✨ 测试完成！"
    # rm -f memory_pool_test
else
    echo "❌ 编译失败！"
    exit 1
fi
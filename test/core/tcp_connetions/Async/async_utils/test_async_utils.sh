#!/bin/bash
# test/tcp_connection/Async/test_async_utils.sh

echo "开始编译和运行异步工具类测试套件..."
echo ""

echo "📦 编译测试程序..."
g++ -std=c++17 \
    -I../../../../../include \
    test_async_utils.cpp \
    ../../../../../src/core/tcp_connections/Async/async_utils.cpp \
    ../../../../../src/core/tcp_connections/Async/async_scheduler_impl.cpp \
    ../../../../../src/core/tcp_connections/Async/async_scheduler.cpp \
    -lgtest -lgtest_main -lpthread \
    -o async_utils_test

if [ $? -eq 0 ]; then
    echo "✅ 编译成功！"
    echo ""
    echo "🚀 开始运行测试..."
    echo ""
    
    ./async_utils_test
    
    echo ""
    echo "✨ 测试完成！"
    # rm -f async_utils_test
else
    echo "❌ 编译失败！"
    exit 1
fi
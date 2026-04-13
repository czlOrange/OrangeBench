#!/bin/bash
# test/core/async/test_factory.sh

echo "开始编译和运行工厂类测试套件..."
echo ""

echo "📦 编译测试程序..."
g++ -std=c++17 \
    -I../../../../../include \
    test_async_factory.cpp \
    ../../../../../src/core/tcp_connections/Async/async_factory.cpp \
    ../../../../../src/core/tcp_connections/Async/async_scheduler_impl.cpp \
    ../../../../../src/core/tcp_connections/Async/async_scheduler.cpp \
    ../../../../../src/core/tcp_connections/Async/async_utils.cpp \
    -lgtest -lgtest_main -lpthread \
    -o factory_test

if [ $? -eq 0 ]; then
    echo "✅ 编译成功！"
    echo ""
    echo "🚀 开始运行测试..."
    echo ""
    
    ./factory_test
    
    echo ""
    echo "✨ 测试完成！"
    # rm -f factory_test
else
    echo "❌ 编译失败！"
    exit 1
fi
#!/bin/bash
# test/core/run_connection_buffer_mgr_test.sh

echo "开始编译和运行 ConnectionBufferManager 测试套件..."
echo ""

echo "📦 编译测试程序..."
g++ -std=c++17 \
    -I../../../../../include \
    test_connection_buffer_mgr.cpp \
    ../../../../../src/core/tcp_connections/Buffer/connection_buffer_mgr.cpp \
    ../../../../../src/core/tcp_connections/Buffer/buffer_factory.cpp \
    ../../../../../src/core/tcp_connections/Buffer/buffer_operator.cpp \
    ../../../../../src/core/tcp_connections/Buffer/buffer.cpp \
    ../../../../../src/core/tcp_connections/Buffer/buffer_view.cpp \
    ../../../../../src/core/tcp_connections/Buffer/memory_pool.cpp \
    -lgtest -lgtest_main -lpthread \
    -o connection_buffer_mgr_test

if [ $? -eq 0 ]; then
    echo "✅ 编译成功！"
    echo ""
    echo "🚀 开始运行测试..."
    echo ""
    
    ./connection_buffer_mgr_test
    
    echo ""
    echo "✨ 测试完成！"
    # rm -f connection_buffer_mgr_test
else
    echo "❌ 编译失败！"
    exit 1
fi
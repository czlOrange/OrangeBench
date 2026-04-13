#!/bin/bash
# test/core/connections/run_io_handler_test.sh

echo "开始编译和运行 IOHandler 测试套件..."
echo ""

echo "📦 编译测试程序..."
g++ -std=c++17 \
    -I../../../../include \
    test_io_handler.cpp \
    ../../../../src/core/tcp_connections/IO/io_handler.cpp \
    -lgtest -lgtest_main -lpthread \
    -o io_handler_test

if [ $? -eq 0 ]; then
    echo "✅ 编译成功！"
    echo ""
    echo "🚀 开始运行测试..."
    echo ""
    
    ./io_handler_test
    
    echo ""
    echo "✨ 测试完成！"
    # rm -f io_handler_test
else
    echo "❌ 编译失败！"
    exit 1
fi
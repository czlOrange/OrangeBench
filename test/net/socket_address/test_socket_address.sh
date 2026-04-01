#!/bin/bash
# test/net/run_simple_test.sh

echo "开始编译和运行网络组件测试套件..."
echo ""

echo "📦 编译测试程序..."
g++ -std=c++17 \
    -I../../../include \
    test_socket_address.cpp \
    ../../../src/net/socket.cpp \
    ../../../src/net/address.cpp \
    -lgtest -lgtest_main -lpthread \
    -o socket_address_test

if [ $? -eq 0 ]; then
    echo "✅ 编译成功！"
    echo ""
    echo "🚀 开始运行测试..."
    echo ""
    
    ./socket_address_test
    
    echo ""
    echo "✨ 测试完成！"
    #rm -f socket_address_test
else
    echo "❌ 编译失败！"
    exit 1
fi
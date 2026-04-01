#!/bin/bash
# test/core/connections/run_connection_interface_test.sh

echo "开始编译和运行 Connection 接口测试套件..."
echo ""

echo "📦 编译测试程序..."
g++ -std=c++17 \
    -I../../../../../include \
    test_connection_interface.cpp \
    -lgtest -lgtest_main -lpthread \
    -o connection_interface_test

if [ $? -eq 0 ]; then
    echo "✅ 编译成功！"
    echo ""
    echo "🚀 开始运行测试..."
    echo ""
    ./connection_interface_test
    echo ""
    echo "✨ 测试完成！"
else
    echo "❌ 编译失败！"
    exit 1
fi
#!/bin/bash
# test/core/async/test_async_operation.sh

echo "开始编译和运行异步操作测试套件..."
echo ""

echo "📦 编译测试程序..."
g++ -std=c++17 \
    -I../../../../../include \
    test_async_operation.cpp \
    -lgtest -lgtest_main -lpthread \
    -o async_operation_test

if [ $? -eq 0 ]; then
    echo "✅ 编译成功！"
    echo ""
    echo "🚀 开始运行测试..."
    echo ""
    
    ./async_operation_test
    
    echo ""
    echo "✨ 测试完成！"
    # rm -f async_operation_test
else
    echo "❌ 编译失败！"
    exit 1
fi
#!/bin/bash
# test/net/run_poller_test.sh

echo "开始编译和运行 Poller 单元测试..."
echo ""

echo "📦 编译测试程序..."
g++ -std=c++17 \
    -I../../../include \
    test_poller.cpp \
    ../../../src/net/poller.cpp \
    -lgtest -lgtest_main -lpthread \
    -o poller_test

if [ $? -eq 0 ]; then
    echo "✅ 编译成功！"
    echo ""
    echo "🚀 开始运行测试..."
    echo ""
    
    ./poller_test
    
    echo ""
    echo "✨ 测试完成！"
    # rm -f poller_test  # 取消注释则删除可执行文件
else
    echo "❌ 编译失败！"
    exit 1
fi
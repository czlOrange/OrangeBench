#!/bin/bash
# test/core/tcp_connections/Event/test_event_dispatcher.sh

echo "开始编译和运行事件分发器测试套件..."
echo ""

echo "📦 编译测试程序..."
g++ -std=c++17 \
    -I../../../../../include \
    test_event_dispatcher.cpp \
    ../../../../../src/core/tcp_connections/Event/event_dispatcher.cpp \
    -lgtest -lgtest_main -lpthread \
    -o event_dispatcher_test

if [ $? -eq 0 ]; then
    echo "✅ 编译成功！"
    echo ""
    echo "🚀 开始运行测试..."
    echo ""
    
    ./event_dispatcher_test
    
    echo ""
    echo "✨ 测试完成！"
    # rm -f event_dispatcher_test
else
    echo "❌ 编译失败！"
    exit 1
fi
#!/bin/bash
# test/run_simple_test.sh

echo "开始编译和运行网络组件测试套件..."
echo ""

# 直接编译测试程序
echo "📦 编译测试程序..."
g++ -std=c++17 -I./include test/net/socket_address.cpp src/net/socket.cpp src/net/address.cpp -o test/net/socket_address
if [ $? -eq 0 ]; then
    echo "✅ 编译成功！"
    echo ""
    echo "🚀 开始运行测试..."
    echo ""
    
    # 运行测试
    ./test/net/socket_address
    
    echo ""
    echo "✨ 测试完成！"
else
    #echo "❌ 编译失败！"
    exit 1
fi

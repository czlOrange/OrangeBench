#!/bin/bash
echo "🔨 编译 HTTP 服务器示例..."

g++ -std=c++17 \
    -I../../include \
    ../src/http_server_example_demo.cpp \
    ../../src/application/http/http_server.cpp \
    ../../src/application/http/http_parser.cpp \
    ../../src/application/http/http_response.cpp \
    ../../src/application/http/http_request.cpp \
    ../../src/application/http/websocket.cpp \
    ../../src/application/http/database.cpp \
    ../../src/core/tcp_connections/IO/io_handler.cpp \
    ../../src/core/tcp_connections/Event/event_dispatcher.cpp \
    -lsqlite3 -lssl -lcrypto -lpthread \
    -o ../build/http_server_example

if [ $? -eq 0 ]; then
    echo "✅ 编译成功！"
    echo "🚀 运行: cd ../build && ./http_server_example"
else
    echo "❌ 编译失败！"
    exit 1
fi

#!/bin/bash

cd "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd ../../

echo "🔨 编译 HTTP 服务器示例..."

g++ -std=c++17 \
    -I./include \
    -I./include/httpserver \
    examples/src/http_server_example_demo.cpp \
    src/application/http/http_server.cpp \
    src/application/http/http_parser.cpp \
    src/application/http/http_response.cpp \
    src/application/http/http_request.cpp \
    src/application/websocket/websocket_session.cpp \
    src/application/websocket/websocket_frame.cpp \
    src/application/websocket/speedtest_ws_handler.cpp \
    src/application/http/http_protocol_handler.cpp \
    src/application/http/handler/SpeedTestApi.cpp \
    src/application/http/handler/AdminPageHandler.cpp \
    src/application/http/http_route_config.cpp \
    src/core/net/io_handler.cpp \
    src/core/Event/event_dispatcher.cpp \
    src/core/Tcp/tcp_connection.cpp \
    src/core/Connection/connection_manager.cpp \
    src/core/Buffer/buffer.cpp \
    src/core/Buffer/buffer_factory.cpp \
    src/core/Buffer/buffer_manager.cpp \
    src/core/Buffer/buffer_operator.cpp \
    src/core/Buffer/buffer_view.cpp \
    src/core/Buffer/connection_buffer_mgr.cpp \
    src/core/Buffer/memory_pool.cpp \
    src/core/Event/connection_event_mgr.cpp \
    src/core/Async/async_scheduler.cpp \
    src/core/Async/async_scheduler_impl.cpp \
    src/core/Async/async_utils.cpp \
    src/infrastructure/database.cpp \
    -lsqlite3 \
    -lssl -lcrypto -lpthread \
    -o examples/build/http_server_example

if [ $? -eq 0 ]; then
    echo "✅ 编译成功！"
    echo "🚀 运行: cd examples/build && ./http_server_example"
else
    echo "❌ 编译失败！"
    exit 1
fi
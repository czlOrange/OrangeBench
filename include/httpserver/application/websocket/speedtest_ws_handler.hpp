// include/httpserver/application/websocket/speedtest_ws_handler.hpp
#pragma once

#include "httpserver/application/websocket/websocket_session.hpp"
#include "httpserver/core/Connection/connection_interface.hpp"
#include <memory>
#include <string>

// 注意：WebSocketSession 在 httpserver::application::http::websocket 命名空间
namespace httpserver::application::websocket {

class SpeedTestWebSocketHandler {
public:
    static std::shared_ptr<httpserver::application::http::websocket::WebSocketSession> CreateAndHandle(
        std::shared_ptr<core::IConnection> conn,
        const std::string& key,
        uint64_t conn_id);
};

} // namespace httpserver::application::websocket
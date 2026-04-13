// include/httpserver/application/http/websocket/websocket_session.hpp
#pragma once

#include "websocket_frame.hpp"
#include "httpserver/core/Connection/connection_interface.hpp"
#include <functional>
#include <memory>
#include <vector>

namespace httpserver::application::http::websocket {

class WebSocketSession : public std::enable_shared_from_this<WebSocketSession> {
public:
    using MessageCallback = std::function<void(const std::string&)>;
    using BinaryCallback = std::function<void(const std::vector<uint8_t>&)>;
    using CloseCallback = std::function<void(uint16_t code, const std::string& reason)>;

    explicit WebSocketSession(std::shared_ptr<core::IConnection> conn);
    ~WebSocketSession();

    // 发送消息
    void SendText(const std::string& text);
    void SendBinary(const std::vector<uint8_t>& data);
    
    // 心跳
    void SendPing();
    void SendPong();
    
    // 关闭连接
    void Close(uint16_t code = 1000, const std::string& reason = "");
    
    // 接收原始数据（由 TCPConnection 回调调用）
    void OnData(std::string_view data);
    
    // 设置回调
    void SetOnMessage(MessageCallback cb) { on_message_ = std::move(cb); }
    void SetOnBinary(BinaryCallback cb) { on_binary_ = std::move(cb); }
    void SetOnClose(CloseCallback cb) { on_close_ = std::move(cb); }
    
    // 握手响应（静态方法）
    static std::string HandshakeResponse(const std::string& key);
    static std::string ComputeAccept(const std::string& key);

private:
    std::shared_ptr<core::IConnection> conn_;
    std::vector<uint8_t> recv_buffer_;
    MessageCallback on_message_;
    BinaryCallback on_binary_;
    CloseCallback on_close_;
    
    void ProcessFrames();
};

} // namespace httpserver::application::http::websocket
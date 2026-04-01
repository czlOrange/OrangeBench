// include/httpserver/application/http/websocket.hpp
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace httpserver::application::http {

// WebSocket 操作码
enum class Opcode : uint8_t {
    CONTINUATION = 0x0,
    TEXT         = 0x1,
    BINARY       = 0x2,
    CLOSE        = 0x8,
    PING         = 0x9,
    PONG         = 0xA
};

// WebSocket 帧
struct WebSocketFrame {
    bool fin;               // 是否最后一帧
    Opcode opcode;
    bool mask;              // 客户端到服务器的帧必须掩码
    uint64_t payload_len;
    uint32_t masking_key;   // 仅当 mask == true
    std::vector<uint8_t> payload;
};

// WebSocket 会话类
class WebSocketSession {
public:
    using MessageCallback = std::function<void(const std::vector<uint8_t>&)>;
    using CloseCallback = std::function<void()>;

    explicit WebSocketSession(int fd);
    ~WebSocketSession();

    // 发送帧
    void sendText(const std::string& text);
    void sendBinary(const std::vector<uint8_t>& data);
    void sendPing();
    void sendPong();
    void close();

    // 接收数据（由 HTTP 服务器调用，传入 TCP 接收到的原始数据）
    void onData(const std::vector<uint8_t>& data);

    // 设置回调
    void setOnMessage(MessageCallback cb) { on_message_ = std::move(cb); }
    void setOnClose(CloseCallback cb) { on_close_ = std::move(cb); }

    // 获取底层 fd（用于写数据）
    int getFd() const { return fd_; }

    // 静态辅助函数：编码帧（不依赖对象实例）
    static std::vector<uint8_t> encodeFrame(const WebSocketFrame& frame);
    
    // 静态辅助函数：编码数据为 WebSocket 帧
    static std::vector<uint8_t> encodeFrameForSend(const std::vector<uint8_t>& data, Opcode opcode);
    
    // 静态辅助函数：计算握手 Accept
    static std::string computeAccept(const std::string& key);
    
    // 静态辅助函数：生成握手响应
    static std::string handshakeResponse(const std::string& key);
    
private:
    int fd_;
    std::vector<uint8_t> recv_buffer_;   // 未处理的数据缓存
    MessageCallback on_message_;
    CloseCallback on_close_;

    // 解析一个帧，返回帧大小（0 表示未完整）
    size_t parseFrame(WebSocketFrame& frame);
    // 发送原始数据
    bool sendRaw(const std::vector<uint8_t>& data);
};

} // namespace httpserver::application::http
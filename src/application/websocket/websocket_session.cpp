// src/application/http/websocket/websocket_session.cpp
#include "httpserver/application/websocket/websocket_session.hpp"
#include <openssl/sha.h>
#include <iostream>

namespace httpserver::application::http::websocket {

// Base64 编码（简单实现）
static std::string base64Encode(const unsigned char* input, int length) {
    static const char* base64_chars = 
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    int i = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];
    
    while (length--) {
        char_array_3[i++] = *(input++);
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;
            for (i = 0; i < 4; i++) result += base64_chars[char_array_4[i]];
            i = 0;
        }
    }
    if (i) {
        for (int j = i; j < 3; j++) char_array_3[j] = '\0';
        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        for (int j = 0; j < i + 1; j++) result += base64_chars[char_array_4[j]];
        while (i++ < 3) result += '=';
    }
    return result;
}

std::string WebSocketSession::ComputeAccept(const std::string& key) {
    std::string accept = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    unsigned char sha[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char*>(accept.c_str()), accept.size(), sha);
    return base64Encode(sha, SHA_DIGEST_LENGTH);
}

std::string WebSocketSession::HandshakeResponse(const std::string& key) {
    std::string accept = ComputeAccept(key);
    return "HTTP/1.1 101 Switching Protocols\r\n"
           "Upgrade: websocket\r\n"
           "Connection: Upgrade\r\n"
           "Sec-WebSocket-Accept: " + accept + "\r\n"
           "\r\n";
}

WebSocketSession::WebSocketSession(std::shared_ptr<core::IConnection> conn)
    : conn_(std::move(conn)) {}

WebSocketSession::~WebSocketSession() {
    Close();
}

void WebSocketSession::SendText(const std::string& text) {
    auto frame = FrameCodec::EncodeText(text);
    conn_->Send(std::string_view(reinterpret_cast<const char*>(frame.data()), frame.size()));
}

void WebSocketSession::SendBinary(const std::vector<uint8_t>& data) {
    auto frame = FrameCodec::EncodeBinary(data);
    conn_->Send(std::string_view(reinterpret_cast<const char*>(frame.data()), frame.size()));
}

void WebSocketSession::SendPing() {
    auto frame = FrameCodec::EncodePing();
    conn_->Send(std::string_view(reinterpret_cast<const char*>(frame.data()), frame.size()));
}

void WebSocketSession::SendPong() {
    auto frame = FrameCodec::EncodePong();
    conn_->Send(std::string_view(reinterpret_cast<const char*>(frame.data()), frame.size()));
}

void WebSocketSession::Close(uint16_t code, const std::string& reason) {
    auto frame = FrameCodec::EncodeClose(code, reason);
    conn_->Send(std::string_view(reinterpret_cast<const char*>(frame.data()), frame.size()));
    conn_->Close();
}

void WebSocketSession::OnData(std::string_view data) {
    // 追加到接收缓冲区
    recv_buffer_.insert(recv_buffer_.end(), data.begin(), data.end());
    ProcessFrames();
}

void WebSocketSession::ProcessFrames() {
    while (!recv_buffer_.empty()) {
        Frame frame;
        size_t consumed = FrameCodec::Decode(recv_buffer_, frame);
        if (consumed == 0) break;  // 数据不完整
        
        // 移除已处理的数据
        recv_buffer_.erase(recv_buffer_.begin(), recv_buffer_.begin() + consumed);
        
        switch (frame.opcode) {
            case Opcode::TEXT: {
                std::string text(frame.payload.begin(), frame.payload.end());
                if (on_message_) on_message_(text);
                break;
            }
            case Opcode::BINARY: {
                if (on_binary_) on_binary_(frame.payload);
                break;
            }
            case Opcode::PING: {
                SendPong();  // 自动回复 pong
                break;
            }
            case Opcode::PONG: {
                // 可以忽略，或用于心跳检测
                break;
            }
            case Opcode::CLOSE: {
                uint16_t code = 1000;
                std::string reason;
                if (frame.payload.size() >= 2) {
                    code = (frame.payload[0] << 8) | frame.payload[1];
                    reason.assign(frame.payload.begin() + 2, frame.payload.end());
                }
                if (on_close_) on_close_(code, reason);
                Close();
                return;
            }
            default:
                // 未知操作码，关闭连接
                Close(1002, "Invalid opcode");
                return;
        }
    }
}

} // namespace httpserver::application::http::websocket
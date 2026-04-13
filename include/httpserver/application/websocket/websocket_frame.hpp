// include/httpserver/application/websocket/websocket_frame.hpp
#pragma once

#include <cstdint>
#include <vector>
#include <string>

namespace httpserver::application::http::websocket {

// WebSocket 操作码
enum class Opcode : uint8_t {
    CONTINUATION = 0x0,
    TEXT         = 0x1,
    BINARY       = 0x2,
    CLOSE        = 0x8,
    PING         = 0x9,
    PONG         = 0xA
};

// WebSocket 帧结构
struct Frame {
    bool fin = true;
    Opcode opcode = Opcode::TEXT;
    bool mask = false;
    uint64_t payload_len = 0;
    uint32_t masking_key = 0;
    std::vector<uint8_t> payload;
};

// 帧编解码器
class FrameCodec {
public:
    // 编码帧
    static std::vector<uint8_t> Encode(const Frame& frame);
    
    // 解码帧（返回解码的字节数，0 表示数据不完整）
    static size_t Decode(const std::vector<uint8_t>& data, Frame& frame);
    
    // 辅助：快速编码数据
    static std::vector<uint8_t> EncodeData(const std::vector<uint8_t>& payload, Opcode opcode, bool fin = true);
    static std::vector<uint8_t> EncodeText(const std::string& text);
    static std::vector<uint8_t> EncodeBinary(const std::vector<uint8_t>& data);
    static std::vector<uint8_t> EncodePing();
    static std::vector<uint8_t> EncodePong();
    static std::vector<uint8_t> EncodeClose(uint16_t code = 1000, const std::string& reason = "");
    
private:
    static uint64_t ReadUint64(const uint8_t* data, size_t offset, size_t bytes);
    static void WriteUint64(std::vector<uint8_t>& out, uint64_t value, size_t bytes);
};

} // namespace httpserver::application::http::websocket
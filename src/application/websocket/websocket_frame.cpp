// src/application/websocket/websocket_frame.cpp
#include "httpserver/application/websocket/websocket_frame.hpp"
#include <cstring>
#include <algorithm>

namespace httpserver::application::http::websocket {

std::vector<uint8_t> FrameCodec::Encode(const Frame& frame) {
    std::vector<uint8_t> out;
    
    // 第一个字节: FIN + RSV1-3 + Opcode
    uint8_t byte0 = (frame.fin ? 0x80 : 0x00) | (static_cast<uint8_t>(frame.opcode) & 0x0F);
    out.push_back(byte0);
    
    // 第二个字节: MASK + PayloadLen
    uint8_t byte1 = (frame.mask ? 0x80 : 0x00);
    uint64_t len = frame.payload_len;
    if (len < 126) {
        byte1 |= static_cast<uint8_t>(len);
        out.push_back(byte1);
    } else if (len <= 0xFFFF) {
        byte1 |= 126;
        out.push_back(byte1);
        WriteUint64(out, len, 2);
    } else {
        byte1 |= 127;
        out.push_back(byte1);
        WriteUint64(out, len, 8);
    }
    
    // Masking key（如果设置了掩码）
    if (frame.mask) {
        WriteUint64(out, frame.masking_key, 4);
    }
    
    // Payload
    out.insert(out.end(), frame.payload.begin(), frame.payload.end());
    
    // 如果设置了掩码，对 payload 进行异或处理
    if (frame.mask && !frame.payload.empty()) {
        uint8_t* data = out.data() + out.size() - frame.payload.size();
        for (size_t i = 0; i < frame.payload.size(); ++i) {
            data[i] ^= reinterpret_cast<const uint8_t*>(&frame.masking_key)[i % 4];
        }
    }
    
    return out;
}

size_t FrameCodec::Decode(const std::vector<uint8_t>& data, Frame& frame) {
    if (data.size() < 2) return 0;  // 至少需要前两个字节
    
    size_t pos = 0;
    uint8_t byte0 = data[pos++];
    frame.fin = (byte0 & 0x80) != 0;
    frame.opcode = static_cast<Opcode>(byte0 & 0x0F);
    
    uint8_t byte1 = data[pos++];
    frame.mask = (byte1 & 0x80) != 0;
    uint64_t payload_len = byte1 & 0x7F;
    
    if (payload_len == 126) {
        if (data.size() < pos + 2) return 0;
        payload_len = ReadUint64(data.data(), pos, 2);
        pos += 2;
    } else if (payload_len == 127) {
        if (data.size() < pos + 8) return 0;
        payload_len = ReadUint64(data.data(), pos, 8);
        pos += 8;
    }
    
    // 获取 masking key（如果存在）
    uint32_t masking_key = 0;
    if (frame.mask) {
        if (data.size() < pos + 4) return 0;
        masking_key = static_cast<uint32_t>(ReadUint64(data.data(), pos, 4));
        pos += 4;
    }
    
    // 检查 payload 是否完整
    if (data.size() < pos + payload_len) return 0;
    
    // 复制 payload
    frame.payload_len = payload_len;
    frame.payload.resize(payload_len);
    std::memcpy(frame.payload.data(), data.data() + pos, payload_len);
    
    // 如果设置了掩码，解码 payload
    if (frame.mask && payload_len > 0) {
        uint8_t* payload_ptr = frame.payload.data();
        for (size_t i = 0; i < payload_len; ++i) {
            payload_ptr[i] ^= reinterpret_cast<const uint8_t*>(&masking_key)[i % 4];
        }
    }
    frame.masking_key = masking_key;
    
    return pos + payload_len;
}

std::vector<uint8_t> FrameCodec::EncodeData(const std::vector<uint8_t>& payload, Opcode opcode, bool fin) {
    Frame frame;
    frame.fin = fin;
    frame.opcode = opcode;
    frame.mask = false;   // 服务器发送不需要掩码
    frame.payload_len = payload.size();
    frame.payload = payload;
    return Encode(frame);
}

std::vector<uint8_t> FrameCodec::EncodeText(const std::string& text) {
    std::vector<uint8_t> payload(text.begin(), text.end());
    return EncodeData(payload, Opcode::TEXT);
}

std::vector<uint8_t> FrameCodec::EncodeBinary(const std::vector<uint8_t>& data) {
    return EncodeData(data, Opcode::BINARY);
}

std::vector<uint8_t> FrameCodec::EncodePing() {
    return EncodeData({}, Opcode::PING);
}

std::vector<uint8_t> FrameCodec::EncodePong() {
    return EncodeData({}, Opcode::PONG);
}

std::vector<uint8_t> FrameCodec::EncodeClose(uint16_t code, const std::string& reason) {
    std::vector<uint8_t> payload;
    payload.push_back((code >> 8) & 0xFF);
    payload.push_back(code & 0xFF);
    payload.insert(payload.end(), reason.begin(), reason.end());
    return EncodeData(payload, Opcode::CLOSE);
}

uint64_t FrameCodec::ReadUint64(const uint8_t* data, size_t offset, size_t bytes) {
    uint64_t value = 0;
    for (size_t i = 0; i < bytes; ++i) {
        value = (value << 8) | data[offset + i];
    }
    return value;
}

void FrameCodec::WriteUint64(std::vector<uint8_t>& out, uint64_t value, size_t bytes) {
    for (size_t i = bytes; i > 0; --i) {
        out.push_back((value >> ((i - 1) * 8)) & 0xFF);
    }
}

} // namespace httpserver::application::http::websocket
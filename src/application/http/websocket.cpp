// src/application/http/websocket.cpp
#include "httpserver/application/http/websocket.hpp"
#include <cstring>
#include <random>
#include <sstream>
#include <iomanip>
#include <unistd.h>
#include <openssl/sha.h>   // 需要链接 -lssl
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>

namespace httpserver::application::http {

// Base64 编码（OpenSSL 辅助）
static std::string base64Encode(const unsigned char* input, int length) {
    BIO *bmem, *b64;
    BUF_MEM *bptr;
    b64 = BIO_new(BIO_f_base64());
    bmem = BIO_new(BIO_s_mem());
    b64 = BIO_push(b64, bmem);
    BIO_write(b64, input, length);
    BIO_flush(b64);
    BIO_get_mem_ptr(b64, &bptr);
    std::string result(bptr->data, bptr->length);
    BIO_free_all(b64);
    // 去除换行符
    result.erase(std::remove(result.begin(), result.end(), '\n'), result.end());
    return result;
}

std::string WebSocketSession::computeAccept(const std::string& key) {
    std::string accept = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    unsigned char sha[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char*>(accept.c_str()), accept.size(), sha);
    return base64Encode(sha, SHA_DIGEST_LENGTH);
}

std::string WebSocketSession::handshakeResponse(const std::string& key) {
    std::ostringstream oss;
    oss << "HTTP/1.1 101 Switching Protocols\r\n"
        << "Upgrade: websocket\r\n"
        << "Connection: Upgrade\r\n"
        << "Sec-WebSocket-Accept: " << computeAccept(key) << "\r\n"
        << "\r\n";
    return oss.str();
}

WebSocketSession::WebSocketSession(int fd) : fd_(fd) {}

WebSocketSession::~WebSocketSession() {
    close();
}

bool WebSocketSession::sendRaw(const std::vector<uint8_t>& data) {
    ssize_t n = write(fd_, data.data(), data.size());
    return n == static_cast<ssize_t>(data.size());
}

std::vector<uint8_t> WebSocketSession::encodeFrame(const WebSocketFrame& frame) {
    std::vector<uint8_t> header;
    header.push_back((frame.fin ? 0x80 : 0x00) | (static_cast<uint8_t>(frame.opcode) & 0x0F));
    uint8_t byte2 = frame.mask ? 0x80 : 0x00;
    if (frame.payload_len < 126) {
        byte2 |= frame.payload_len;
        header.push_back(byte2);
    } else if (frame.payload_len <= 0xFFFF) {
        byte2 |= 126;
        header.push_back(byte2);
        header.push_back((frame.payload_len >> 8) & 0xFF);
        header.push_back(frame.payload_len & 0xFF);
    } else {
        byte2 |= 127;
        header.push_back(byte2);
        for (int i = 7; i >= 0; --i)
            header.push_back((frame.payload_len >> (i * 8)) & 0xFF);
    }
    if (frame.mask) {
        for (int i = 0; i < 4; ++i)
            header.push_back((frame.masking_key >> (i * 8)) & 0xFF);
    }
    std::vector<uint8_t> out = header;
    if (frame.mask) {
        for (size_t i = 0; i < frame.payload.size(); ++i)
            out.push_back(frame.payload[i] ^ ((frame.masking_key >> ((i % 4) * 8)) & 0xFF));
    } else {
        out.insert(out.end(), frame.payload.begin(), frame.payload.end());
    }
    return out;
}

size_t WebSocketSession::parseFrame(WebSocketFrame& frame) {
    if (recv_buffer_.size() < 2) return 0;
    size_t pos = 0;
    frame.fin = (recv_buffer_[0] & 0x80) != 0;
    frame.opcode = static_cast<Opcode>(recv_buffer_[0] & 0x0F);
    frame.mask = (recv_buffer_[1] & 0x80) != 0;
    uint64_t len = recv_buffer_[1] & 0x7F;
    pos = 2;
    if (len == 126) {
        if (recv_buffer_.size() < pos + 2) return 0;
        len = (recv_buffer_[pos] << 8) | recv_buffer_[pos+1];
        pos += 2;
    } else if (len == 127) {
        if (recv_buffer_.size() < pos + 8) return 0;
        len = 0;
        for (int i = 0; i < 8; ++i)
            len = (len << 8) | recv_buffer_[pos+i];
        pos += 8;
    }
    frame.payload_len = len;
    if (frame.mask) {
        if (recv_buffer_.size() < pos + 4) return 0;
        frame.masking_key = 0;
        for (int i = 0; i < 4; ++i)
            frame.masking_key |= (recv_buffer_[pos+i] << (i*8));
        pos += 4;
    }
    if (recv_buffer_.size() < pos + len) return 0;
    frame.payload.resize(len);
    std::copy(recv_buffer_.begin() + pos, recv_buffer_.begin() + pos + len, frame.payload.begin());
    if (frame.mask) {
        for (size_t i = 0; i < len; ++i)
            frame.payload[i] ^= ((frame.masking_key >> ((i % 4) * 8)) & 0xFF);
    }
    return pos + len;
}

void WebSocketSession::onData(const std::vector<uint8_t>& data) {
    recv_buffer_.insert(recv_buffer_.end(), data.begin(), data.end());
    while (true) {
        WebSocketFrame frame;
        size_t consumed = parseFrame(frame);
        if (consumed == 0) break;
        recv_buffer_.erase(recv_buffer_.begin(), recv_buffer_.begin() + consumed);
        switch (frame.opcode) {
            case Opcode::PING:
                sendPong();
                break;
            case Opcode::PONG:
                // 可忽略或传递给上层
                break;
            case Opcode::CLOSE:
                close();
                if (on_close_) on_close_();
                return;
            case Opcode::TEXT:
            case Opcode::BINARY:
                if (on_message_) on_message_(frame.payload);
                break;
            default:
                break;
        }
    }
}

void WebSocketSession::sendText(const std::string& text) {
    WebSocketFrame frame;
    frame.fin = true;
    frame.opcode = Opcode::TEXT;
    frame.mask = false; // 服务器到客户端不需要掩码
    frame.payload_len = text.size();
    frame.payload.assign(text.begin(), text.end());
    auto out = encodeFrame(frame);
    sendRaw(out);
}

void WebSocketSession::sendBinary(const std::vector<uint8_t>& data) {
    WebSocketFrame frame;
    frame.fin = true;
    frame.opcode = Opcode::BINARY;
    frame.mask = false;
    frame.payload_len = data.size();
    frame.payload = data;
    auto out = encodeFrame(frame);
    sendRaw(out);
}

void WebSocketSession::sendPing() {
    WebSocketFrame frame;
    frame.fin = true;
    frame.opcode = Opcode::PING;
    frame.mask = false;
    frame.payload_len = 0;
    auto out = encodeFrame(frame);
    sendRaw(out);
}

void WebSocketSession::sendPong() {
    WebSocketFrame frame;
    frame.fin = true;
    frame.opcode = Opcode::PONG;
    frame.mask = false;
    frame.payload_len = 0;
    auto out = encodeFrame(frame);
    sendRaw(out);
}

void WebSocketSession::close() {
    if (fd_ >= 0) {
        WebSocketFrame frame;
        frame.fin = true;
        frame.opcode = Opcode::CLOSE;
        frame.mask = false;
        frame.payload_len = 0;
        auto out = encodeFrame(frame);
        sendRaw(out);
        ::close(fd_);
        fd_ = -1;
    }
}

std::vector<uint8_t> WebSocketSession::encodeFrameForSend(const std::vector<uint8_t>& data, Opcode opcode) {
    WebSocketFrame frame;
    frame.fin = true;
    frame.opcode = opcode;
    frame.mask = false;  // 服务器到客户端不需要掩码
    frame.payload_len = data.size();
    frame.payload = data;
    return encodeFrame(frame);
}
} // namespace httpserver::application::http
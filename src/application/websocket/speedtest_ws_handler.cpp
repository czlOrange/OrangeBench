// src/application/websocket/speedtest_ws_handler.cpp
#include "httpserver/application/websocket/speedtest_ws_handler.hpp"
#include "httpserver/application/websocket/websocket_frame.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <string>

namespace httpserver::application::websocket {

using WsSession = httpserver::application::http::websocket::WebSocketSession;
using FrameCodec = httpserver::application::http::websocket::FrameCodec;

// 简单 JSON 辅助（仅用于解析 ping）
static std::string extractJsonString(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return "";
    pos = json.find(":", pos);
    if (pos == std::string::npos) return "";
    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    if (pos >= json.size()) return "";
    if (json[pos] == '"') {
        pos++;
        size_t end = json.find("\"", pos);
        if (end == std::string::npos) return "";
        return json.substr(pos, end - pos);
    } else {
        size_t end = json.find_first_of(",}", pos);
        if (end == std::string::npos) end = json.size();
        std::string value = json.substr(pos, end - pos);
        while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) value.pop_back();
        return value;
    }
}

static int extractJsonInt(const std::string& json, const std::string& key, int default_val = 0) {
    std::string val = extractJsonString(json, key);
    if (val.empty()) return default_val;
    try { return std::stoi(val); } catch (...) { return default_val; }
}

static std::string getJsonType(const std::string& json) {
    return extractJsonString(json, "type");
}

std::shared_ptr<WsSession> SpeedTestWebSocketHandler::CreateAndHandle(
    std::shared_ptr<core::IConnection> conn,
    const std::string& key,
    uint64_t conn_id) {

    // 发送握手响应
    std::string response = WsSession::HandshakeResponse(key);
    auto send_result = conn->Send(response);
    if (send_result.has_error()) {
        std::cerr << "Failed to send WebSocket handshake response for conn " << conn_id << std::endl;
        conn->Close();
        return nullptr;
    }

    std::cout << "✅ WebSocket handshake successful for conn " << conn_id << std::endl;

    auto ws_session = std::make_shared<WsSession>(conn);

    // 设置消息回调（处理 ping）- 注意捕获 ws_session
    ws_session->SetOnMessage([ws_session](const std::string& msg) {
        std::string type = getJsonType(msg);
        if (type == "ping") {
            int seq = extractJsonInt(msg, "seq", -1);
            std::string pong = "{\"type\":\"pong\",\"seq\":" + std::to_string(seq) + "}";
            ws_session->SendText(pong);
        }
    });

    ws_session->SetOnBinary([](const std::vector<uint8_t>& data) {
        // 客户端上传数据，无需处理
    });

    ws_session->SetOnClose([conn_id](uint16_t code, const std::string& reason) {
        std::cout << "🔌 WebSocket closed: conn=" << conn_id << ", code=" << code << std::endl;
    });

    // 启动测速下载线程（持续推送二进制数据）
    std::thread([conn, conn_id, ws_session]() {
        const size_t chunk_size = 64 * 1024;
        std::vector<uint8_t> test_data(chunk_size, 0xAA);
        auto start_time = std::chrono::steady_clock::now();
        const int duration_seconds = 10;

        while (true) {
            auto frame = FrameCodec::EncodeBinary(test_data);
            auto result = conn->Send(std::string_view(reinterpret_cast<const char*>(frame.data()), frame.size()));

            if (result.has_error()) {
                std::error_code ec = result.error();
                if (ec == std::errc::resource_unavailable_try_again ||
                    ec == std::errc::operation_would_block) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    continue;
                } else {
                    break;
                }
            }

            if (std::chrono::steady_clock::now() - start_time >= std::chrono::seconds(duration_seconds)) {
                break;
            }
        }

        auto close_frame = FrameCodec::EncodeClose(1000, "Test finished");
        conn->Send(std::string_view(reinterpret_cast<const char*>(close_frame.data()), close_frame.size()));
        std::cout << "📊 Speed test download transmission completed for conn " << conn_id << std::endl;
    }).detach();

    return ws_session;
}

} // namespace httpserver::application::websocket
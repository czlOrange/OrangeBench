// src/application/http/http_server.cpp
#include "httpserver/application/http/http_server.hpp"
#include "httpserver/application/http/http_parser.hpp"
#include "httpserver/application/http/http_response.hpp"
#include "httpserver/core/tcp_connections/Connection/connection_interface.hpp"
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <thread>
#include <chrono>
#include <openssl/sha.h>
#include <random>
#include <fstream>
#include <vector>
#include <sstream>

namespace {
    // Base64 编码函数
    std::string base64Encode(const unsigned char* input, int length) {
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
                for (i = 0; i < 4; i++)
                    result += base64_chars[char_array_4[i]];
                i = 0;
            }
        }
        if (i) {
            for (int j = i; j < 3; j++)
                char_array_3[j] = '\0';
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            for (int j = 0; j < i + 1; j++)
                result += base64_chars[char_array_4[j]];
            while (i++ < 3)
                result += '=';
        }
        return result;
    }
    
    // 计算 WebSocket Accept
    std::string computeWebSocketAccept(const std::string& key) {
        std::string accept = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
        unsigned char sha[SHA_DIGEST_LENGTH];
        SHA1(reinterpret_cast<const unsigned char*>(accept.c_str()), accept.size(), sha);
        return base64Encode(sha, SHA_DIGEST_LENGTH);
    }

    // WebSocket 帧编码
    std::vector<uint8_t> encodeWebSocketFrame(const std::vector<uint8_t>& payload, bool fin = true, uint8_t opcode = 0x02) {
        std::vector<uint8_t> frame;
        uint8_t first = (fin ? 0x80 : 0x00) | (opcode & 0x0F);
        frame.push_back(first);
        
        size_t len = payload.size();
        if (len < 126) {
            frame.push_back(static_cast<uint8_t>(len));
        } else if (len <= 0xFFFF) {
            frame.push_back(126);
            frame.push_back((len >> 8) & 0xFF);
            frame.push_back(len & 0xFF);
        } else {
            frame.push_back(127);
            for (int i = 7; i >= 0; --i) {
                frame.push_back((len >> (i * 8)) & 0xFF);
            }
        }
        
        frame.insert(frame.end(), payload.begin(), payload.end());
        return frame;
    }
}

namespace httpserver::application::http {

HttpServer::HttpServer(std::shared_ptr<core::IEventDispatcher> dispatcher)
    : dispatcher_(std::move(dispatcher))
    , io_handler_(core::IIOHandler::CreateDefault())
    , listen_fd_(-1)
    , running_(false) {
    db_.init(); // 初始化数据库
}

HttpServer::~HttpServer() {
    if (listen_fd_ >= 0) {
        close(listen_fd_);
    }
}

void HttpServer::Get(const std::string& path, HttpHandler handler) {
    get_handlers_[path] = std::move(handler);
}

void HttpServer::Post(const std::string& path, HttpHandler handler) {
    post_handlers_[path] = std::move(handler);
}

bool HttpServer::createListeningSocket(const std::string& host, uint16_t port) {
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        std::cerr << "Failed to create socket: " << strerror(errno) << std::endl;
        return false;
    }

    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    int flags = fcntl(listen_fd_, F_GETFL, 0);
    fcntl(listen_fd_, F_SETFL, flags | O_NONBLOCK);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    
    if (host == "0.0.0.0") {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
    }

    if (bind(listen_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "Failed to bind: " << strerror(errno) << std::endl;
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    if (listen(listen_fd_, SOMAXCONN) < 0) {
        std::cerr << "Failed to listen: " << strerror(errno) << std::endl;
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    return true;
}

void HttpServer::acceptLoop() {
    while (running_) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(listen_fd_, (struct sockaddr*)&client_addr, &client_len);
        
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            continue;
        }

        int flags = fcntl(client_fd, F_GETFL, 0);
        fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);

        char buffer[8192];
        ssize_t n = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        if (n > 0) {
            buffer[n] = '\0';
            std::string request_data(buffer, n);
            
            // 检测 WebSocket 升级请求
            if (request_data.find("Upgrade: websocket") != std::string::npos) {
                // 解析 Sec-WebSocket-Key
                std::string key;
                size_t key_pos = request_data.find("Sec-WebSocket-Key:");
                if (key_pos != std::string::npos) {
                    size_t start = key_pos + 19;
                    size_t end = request_data.find("\r\n", start);
                    if (end != std::string::npos) {
                        key = request_data.substr(start, end - start);
                        key.erase(0, key.find_first_not_of(" \t"));
                        key.erase(key.find_last_not_of(" \t") + 1);
                    }
                }
                
                std::string accept = computeWebSocketAccept(key);
                std::string response = 
                    "HTTP/1.1 101 Switching Protocols\r\n"
                    "Upgrade: websocket\r\n"
                    "Connection: Upgrade\r\n"
                    "Sec-WebSocket-Accept: " + accept + "\r\n"
                    "\r\n";
                send(client_fd, response.c_str(), response.size(), 0);
                std::cout << "✅ WebSocket 握手成功" << std::endl;
                
                // 启动数据推送线程
                std::thread([client_fd]() {
                    std::vector<uint8_t> test_data(65536);
                    for (auto& c : test_data) c = rand() % 256;
                    
                    auto start_time = std::chrono::steady_clock::now();
                    uint32_t seq = 0;
                    
                    while (true) {
                        std::vector<uint8_t> payload(4 + test_data.size());
                        payload[0] = (seq >> 24) & 0xFF;
                        payload[1] = (seq >> 16) & 0xFF;
                        payload[2] = (seq >> 8) & 0xFF;
                        payload[3] = seq & 0xFF;
                        std::copy(test_data.begin(), test_data.end(), payload.begin() + 4);
                        
                        auto frame = encodeWebSocketFrame(payload, true, 0x02);
                        send(client_fd, frame.data(), frame.size(), 0);
                        seq++;
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                        
                        auto elapsed = std::chrono::steady_clock::now() - start_time;
                        if (elapsed > std::chrono::seconds(10)) break;
                    }
                    
                    auto close_frame = encodeWebSocketFrame({}, true, 0x08);
                    send(client_fd, close_frame.data(), close_frame.size(), 0);
                    close(client_fd);
                }).detach();
                continue;
            }
            
            // HTTP 处理
            HttpRequest request;
            int parsed = HttpParser::Parse(request_data, request);
            
            HttpResponse response;
            if (parsed > 0) {
                std::string method = request.GetMethod();
                std::string path = request.GetPath();
                
                // 静态文件处理
                if (method == "GET") {
                    std::string file_path;
                    if (path == "/") {
                        file_path = "../public/index.html";
                    } else {
                        file_path = "../public" + path;
                    }
                    std::ifstream file(file_path, std::ios::binary);
                    if (file.is_open()) {
                        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                        file.close();
                        response.SetStatus(200);
                        response.SetBody(content);
                        if (file_path.find(".html") != std::string::npos) {
                            response.SetContentType("text/html");
                        } else if (file_path.find(".css") != std::string::npos) {
                            response.SetContentType("text/css");
                        } else if (file_path.find(".js") != std::string::npos) {
                            response.SetContentType("application/javascript");
                        } else {
                            response.SetContentType("text/plain");
                        }
                        std::string resp_str = response.ToString();
                        send(client_fd, resp_str.c_str(), resp_str.size(), 0);
                        close(client_fd);
                        continue;
                    }
                }
                
                // 路由匹配
                auto it = get_handlers_.find(path);
                auto post_it = post_handlers_.find(path);
                if (method == "GET" && it != get_handlers_.end()) {
                    it->second(request, response);
                } else if (method == "POST" && post_it != post_handlers_.end()) {
                    post_it->second(request, response);
                } else {
                    response.SetStatus(404);
                    response.SetBody("Not Found");
                }
            } else {
                response.SetStatus(400);
                response.SetBody("Bad Request");
            }
            
            std::string resp_str = response.ToString();
            send(client_fd, resp_str.c_str(), resp_str.size(), 0);
        }
        close(client_fd);
    }
}

void HttpServer::Listen(const std::string& host, uint16_t port) {
    if (!createListeningSocket(host, port)) {
        std::cerr << "Failed to start server" << std::endl;
        return;
    }

    running_ = true;
    std::cout << "✅ HTTP server listening on " << host << ":" << port << std::endl;
    std::cout << "   GET routes:" << std::endl;
    for (const auto& [path, _] : get_handlers_) {
        std::cout << "     - GET " << path << std::endl;
    }
    std::cout << "   POST routes:" << std::endl;
    for (const auto& [path, _] : post_handlers_) {
        std::cout << "     - POST " << path << std::endl;
    }
    std::cout << std::endl;

    // 注册 API 路由
    Post("/api/record", [this](const HttpRequest& req, HttpResponse& resp) { handleSaveRecord(req, resp); });
    Get("/api/stats", [this](const HttpRequest& req, HttpResponse& resp) { handleGetStats(req, resp); });
    Get("/api/devices", [this](const HttpRequest& req, HttpResponse& resp) { handleGetDevices(req, resp); });
    Get("/api/history", [this](const HttpRequest& req, HttpResponse& resp) { handleGetHistory(req, resp); });
    Get("/admin", [this](const HttpRequest& req, HttpResponse& resp) { handleAdminPage(req, resp); });

    acceptLoop();
}

void HttpServer::onNewConnection(std::shared_ptr<core::IConnection> conn) {
    conn->SetDataCallback([this](std::shared_ptr<core::IConnection> c, std::string_view data) {
        onData(c, data);
    });
    conn->SetErrorCallback([this](std::shared_ptr<core::IConnection> c, std::error_code ec) {
        onError(c, ec);
    });
}

void HttpServer::onData(std::shared_ptr<core::IConnection> conn, std::string_view data) {
    std::string request_data(data.data(), data.size());
    
    if (request_data.find("Upgrade: websocket") != std::string::npos) {
        handleWebSocketUpgrade(conn, request_data);
        return;
    }
    
    HttpRequest request;
    int parsed = HttpParser::Parse(request_data, request);
    if (parsed < 0) {
        HttpResponse resp;
        resp.SetStatus(400);
        resp.SetBody("Bad Request");
        conn->Send(resp.ToString());
        conn->Close();
        return;
    }
    if (parsed == 0) {
        return;
    }

    std::string method = request.GetMethod();
    std::string path = request.GetPath();
    HttpResponse response;
    
    if (method == "GET") {
        std::string file_path;
        if (path == "/") {
            file_path = "../public/index.html";
        } else {
            file_path = "../public" + path;
        }
        std::ifstream file(file_path, std::ios::binary);
        if (file.is_open()) {
            std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            file.close();
            response.SetStatus(200);
            response.SetBody(content);
            if (file_path.find(".html") != std::string::npos) response.SetContentType("text/html");
            else if (file_path.find(".css") != std::string::npos) response.SetContentType("text/css");
            else if (file_path.find(".js") != std::string::npos) response.SetContentType("application/javascript");
            else response.SetContentType("text/plain");
            conn->Send(response.ToString());
            conn->Close();
            return;
        }
    }
    
    auto get_it = get_handlers_.find(path);
    auto post_it = post_handlers_.find(path);
    if (method == "GET" && get_it != get_handlers_.end()) {
        get_it->second(request, response);
    } else if (method == "POST" && post_it != post_handlers_.end()) {
        post_it->second(request, response);
    } else {
        response.SetStatus(404);
        response.SetBody("Not Found");
    }
    
    response.SetContentType("text/plain");
    conn->Send(response.ToString());
    conn->Close();
}

void HttpServer::onError(std::shared_ptr<core::IConnection> conn, std::error_code ec) {
    std::cerr << "Connection error: " << ec.message() << std::endl;
    conn->Close();
}

void HttpServer::handleWebSocketUpgrade(std::shared_ptr<core::IConnection> conn, const std::string& request) {
    std::string key;
    size_t key_pos = request.find("Sec-WebSocket-Key:");
    if (key_pos != std::string::npos) {
        size_t start = key_pos + 19;
        size_t end = request.find("\r\n", start);
        if (end != std::string::npos) {
            key = request.substr(start, end - start);
            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t") + 1);
        }
    }
    
    std::string accept = computeWebSocketAccept(key);
    std::string response = 
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " + accept + "\r\n"
        "\r\n";
    conn->Send(response);
    std::cout << "✅ WebSocket 握手成功" << std::endl;
}

void HttpServer::startSpeedTestSession(std::shared_ptr<core::IConnection> conn, const std::string& path) {}

void HttpServer::ServeStatic(const std::string& url_prefix, const std::string& root_dir) {
    std::cout << "   STATIC: " << url_prefix << " -> " << root_dir << std::endl;
}

// ========== API 实现 ==========

void HttpServer::handleSaveRecord(const HttpRequest& req, HttpResponse& resp) {
    std::string body = req.GetBody();
    SpeedRecord record;
    record.device_id = "device_" + std::to_string(rand());
    record.device_name = "Unknown";
    record.speed_mbps = 0;
    record.latency_ms = 0;
    record.packet_loss = 0;
    record.total_bytes = 0;
    record.test_duration = 10;
    
    // 简单 JSON 解析
    auto findValue = [&](const std::string& key) -> std::string {
        size_t pos = body.find("\"" + key + "\"");
        if (pos == std::string::npos) return "";
        pos = body.find(":", pos);
        if (pos == std::string::npos) return "";
        pos++;
        while (pos < body.size() && (body[pos] == ' ' || body[pos] == '\t')) pos++;
        if (body[pos] == '"') {
            pos++;
            size_t end = body.find("\"", pos);
            return body.substr(pos, end - pos);
        } else {
            size_t end = body.find_first_of(",}", pos);
            return body.substr(pos, end - pos);
        }
    };
    
    std::string speed_str = findValue("speed");
    if (!speed_str.empty()) record.speed_mbps = std::stod(speed_str);
    std::string dev_id = findValue("device_id");
    if (!dev_id.empty()) record.device_id = dev_id;
    std::string dev_name = findValue("device_name");
    if (!dev_name.empty()) record.device_name = dev_name;
    
    if (db_.saveSpeedRecord(record)) {
        resp.SetBody("{\"status\":\"ok\"}");
    } else {
        resp.SetStatus(500);
        resp.SetBody("{\"status\":\"error\"}");
    }
    resp.SetContentType("application/json");
}

void HttpServer::handleGetStats(const HttpRequest& req, HttpResponse& resp) {
    auto stats = db_.getStats();
    char buf[512];
    snprintf(buf, sizeof(buf),
        "{\"total_tests\":%d,\"avg_speed\":%.2f,\"max_speed\":%.2f,\"active_devices\":%d,\"total_bytes\":%ld}",
        stats.total_tests, stats.avg_speed, stats.max_speed, stats.active_devices, stats.total_bytes_transferred);
    resp.SetBody(buf);
    resp.SetContentType("application/json");
}

void HttpServer::handleGetDevices(const HttpRequest& req, HttpResponse& resp) {
    auto devices = db_.getDevices();
    std::string json = "[";
    for (size_t i = 0; i < devices.size(); ++i) {
        if (i > 0) json += ",";
        json += "{\"device_id\":\"" + devices[i].device_id + "\","
                "\"device_name\":\"" + devices[i].device_name + "\","
                "\"last_seen\":\"" + devices[i].last_seen + "\","
                "\"total_tests\":" + std::to_string(devices[i].total_tests) + ","
                "\"avg_speed\":" + std::to_string(devices[i].avg_speed) + "}";
    }
    json += "]";
    resp.SetBody(json);
    resp.SetContentType("application/json");
}

void HttpServer::handleGetHistory(const HttpRequest& req, HttpResponse& resp) {
    auto records = db_.getHistory(50);
    std::string json = "[";
    for (size_t i = 0; i < records.size(); ++i) {
        if (i > 0) json += ",";
        json += "{\"id\":" + std::to_string(records[i].id) + ","
                "\"device_name\":\"" + records[i].device_name + "\","
                "\"speed_mbps\":" + std::to_string(records[i].speed_mbps) + ","
                "\"created_at\":\"" + records[i].created_at + "\"}";
    }
    json += "]";
    resp.SetBody(json);
    resp.SetContentType("application/json");
}

void HttpServer::handleAdminPage(const HttpRequest& req, HttpResponse& resp) {
    std::string html = 
        "<!DOCTYPE html>\n"
        "<html>\n"
        "<head>\n"
        "    <meta charset=\"UTF-8\">\n"
        "    <title>测速后台管理</title>\n"
        "    <style>\n"
        "        * { box-sizing: border-box; }\n"
        "        body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background: #f0f2f5; margin: 0; padding: 20px; }\n"
        "        .container { max-width: 1400px; margin: 0 auto; }\n"
        "        h1 { color: #1a1a2e; }\n"
        "        .stats-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 20px; margin-bottom: 30px; }\n"
        "        .stat-card { background: white; border-radius: 12px; padding: 20px; box-shadow: 0 2px 8px rgba(0,0,0,0.1); text-align: center; }\n"
        "        .stat-number { font-size: 36px; font-weight: bold; color: #667eea; }\n"
        "        .stat-label { color: #666; margin-top: 8px; }\n"
        "        .card { background: white; border-radius: 12px; padding: 20px; margin-bottom: 30px; box-shadow: 0 2px 8px rgba(0,0,0,0.1); }\n"
        "        .card h3 { margin-top: 0; color: #333; }\n"
        "        table { width: 100%; border-collapse: collapse; }\n"
        "        th, td { padding: 12px; text-align: left; border-bottom: 1px solid #eee; }\n"
        "        th { background: #667eea; color: white; }\n"
        "        button { background: #667eea; color: white; border: none; padding: 8px 16px; border-radius: 6px; cursor: pointer; margin-top: 10px; }\n"
        "        button:hover { background: #5a67d8; }\n"
        "        .refresh-btn { margin-bottom: 15px; }\n"
        "    </style>\n"
        "    <script src=\"https://cdn.jsdelivr.net/npm/chart.js\"></script>\n"
        "</head>\n"
        "<body>\n"
        "<div class=\"container\">\n"
        "    <h1>📊 测速后台管理</h1>\n"
        "    <div class=\"stats-grid\" id=\"stats\"></div>\n"
        "    <div class=\"card\">\n"
        "        <h3>📱 设备列表</h3>\n"
        "        <button onclick=\"loadDevices()\" class=\"refresh-btn\">刷新</button>\n"
        "        <table id=\"devices-table\">\n"
        "            <thead>\n"
        "                <tr><th>设备名称</th><th>最后测试</th><th>测试次数</th><th>平均速度 (Mbps)</th></tr>\n"
        "            </thead>\n"
        "            <tbody></tbody>\n"
        "        </table>\n"
        "    </div>\n"
        "    <div class=\"card\">\n"
        "        <h3>📜 历史记录</h3>\n"
        "        <button onclick=\"loadHistory()\" class=\"refresh-btn\">刷新</button>\n"
        "        <table id=\"history-table\">\n"
        "            <thead>\n"
        "                <tr><th>时间</th><th>设备</th><th>速度 (Mbps)</th></tr>\n"
        "            </thead>\n"
        "            <tbody></tbody>\n"
        "        </table>\n"
        "    </div>\n"
        "</div>\n"
        "<script>\n"
        "    async function loadStats() {\n"
        "        const res = await fetch('/api/stats');\n"
        "        const stats = await res.json();\n"
        "        document.getElementById('stats').innerHTML = \n"
        "            '<div class=\"stat-card\"><div class=\"stat-number\">' + stats.total_tests + '</div><div class=\"stat-label\">总测试次数</div></div>' +\n"
        "            '<div class=\"stat-card\"><div class=\"stat-number\">' + stats.avg_speed.toFixed(1) + '</div><div class=\"stat-label\">平均速度 (Mbps)</div></div>' +\n"
        "            '<div class=\"stat-card\"><div class=\"stat-number\">' + stats.max_speed.toFixed(1) + '</div><div class=\"stat-label\">最高速度 (Mbps)</div></div>' +\n"
        "            '<div class=\"stat-card\"><div class=\"stat-number\">' + stats.active_devices + '</div><div class=\"stat-label\">活跃设备</div></div>' +\n"
        "            '<div class=\"stat-card\"><div class=\"stat-number\">' + (stats.total_bytes / 1e6).toFixed(1) + '</div><div class=\"stat-label\">总传输 (MB)</div></div>';\n"
        "    }\n"
        "    async function loadDevices() {\n"
        "        const res = await fetch('/api/devices');\n"
        "        const devices = await res.json();\n"
        "        let html = '';\n"
        "        for (const d of devices) {\n"
        "            html += '<tr><td>' + escapeHtml(d.device_name) + '</td><td>' + d.last_seen + '</td><td>' + d.total_tests + '</td><td>' + d.avg_speed.toFixed(1) + '</td></tr>';\n"
        "        }\n"
        "        document.querySelector('#devices-table tbody').innerHTML = html;\n"
        "    }\n"
        "    async function loadHistory() {\n"
        "        const res = await fetch('/api/history');\n"
        "        const records = await res.json();\n"
        "        let html = '';\n"
        "        for (const r of records) {\n"
        "            html += '<tr><td>' + r.created_at + '</td><td>' + escapeHtml(r.device_name) + '</td><td>' + r.speed_mbps.toFixed(1) + '</td></tr>';\n"
        "        }\n"
        "        document.querySelector('#history-table tbody').innerHTML = html;\n"
        "    }\n"
        "    function escapeHtml(str) {\n"
        "        if (!str) return '';\n"
        "        return str.replace(/[&<>]/g, function(m) {\n"
        "            if (m === '&') return '&amp;';\n"
        "            if (m === '<') return '&lt;';\n"
        "            if (m === '>') return '&gt;';\n"
        "            return m;\n"
        "        });\n"
        "    }\n"
        "    loadStats();\n"
        "    loadDevices();\n"
        "    loadHistory();\n"
        "    setInterval(loadStats, 5000);\n"
        "</script>\n"
        "</body>\n"
        "</html>";
    resp.SetBody(html);
    resp.SetContentType("text/html");
}

} // namespace httpserver::application::http
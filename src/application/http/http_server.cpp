// src/application/http/http_server.cpp
#include "httpserver/application/http/http_server.hpp"
#include "httpserver/core/Tcp/tcp_connection.hpp"
#include "httpserver/application/http/http_protocol_handler.hpp"
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <chrono>
#include <fstream>
#include <sstream>
#include <filesystem>

namespace fs = std::filesystem;

namespace {

bool readFile(const std::string& path, std::string& content) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;
    content.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return true;
}

} // anonymous namespace

namespace httpserver::application::http {

HttpServer::HttpServer(std::shared_ptr<core::IEventDispatcher> dispatcher)
    : dispatcher_(std::move(dispatcher))
    , io_handler_(core::IIOHandler::CreateDefault())
    , buffer_manager_(core::IBufferManager::CreateDefault())
    , scheduler_(core::async::IScheduler::CreateDefault())
    , connection_manager_(std::make_shared<core::DefaultConnectionManager>())
    , protocol_handler_(std::make_shared<HttpProtocolHandler>(dispatcher_))
    , listen_fd_(-1)
    , running_(false) {
    db_.init();
    if (scheduler_) {
        scheduler_->Start();
    }
    protocol_handler_->Start();
}

HttpServer::~HttpServer() {
    Stop();
    if (listen_fd_ >= 0) {
        close(listen_fd_);
    }
}

void HttpServer::Get(const std::string& path, HttpHandler handler) {
    // 为了兼容，仍然存储，但实际不再使用
    get_handlers_[path] = std::move(handler);
    std::cout << "   ✅ GET " << path << std::endl;
}

void HttpServer::Post(const std::string& path, HttpHandler handler) {
    post_handlers_[path] = std::move(handler);
    std::cout << "   ✅ POST " << path << std::endl;
}

void HttpServer::ServeStatic(const std::string& url_prefix, const std::string& root_dir) {
    static_configs_.push_back({url_prefix, root_dir});
    protocol_handler_->ServeStatic(url_prefix, root_dir);
    std::cout << "   📁 STATIC: " << url_prefix << " -> " << root_dir << std::endl;
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
    std::cout << "✅ HttpServer::acceptLoop thread started, listen_fd = " << listen_fd_ << std::endl;
    
    if (listen_fd_ < 0) {
        std::cerr << "❌ listen_fd is invalid!" << std::endl;
        return;
    }
    
    fd_set read_fds;
    struct timeval tv;
    
    while (running_) {
        FD_ZERO(&read_fds);
        FD_SET(listen_fd_, &read_fds);
        tv.tv_sec = 0;
        tv.tv_usec = 100000;
        
        int ret = select(listen_fd_ + 1, &read_fds, nullptr, nullptr, &tv);
        
        if (ret < 0) {
            if (errno == EINTR) continue;
            std::cerr << "select error: " << strerror(errno) << std::endl;
            continue;
        }
        
        if (ret == 0) continue;
        
        if (FD_ISSET(listen_fd_, &read_fds)) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int client_fd = accept(listen_fd_, (struct sockaddr*)&client_addr, &client_len);
            
            if (client_fd < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
                std::cerr << "accept error: " << strerror(errno) << std::endl;
                continue;
            }
            
            std::cout << "✅ New connection accepted! fd = " << client_fd << std::endl;
            
            int flags = fcntl(client_fd, F_GETFL, 0);
            fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
            
            std::string client_ip = inet_ntoa(client_addr.sin_addr);
            uint16_t client_port = ntohs(client_addr.sin_port);
            core::SocketAddress peer_addr = core::SocketAddress::FromIpPort(client_ip, client_port);
            
            auto conn = std::make_shared<core::TCPConnection>(
                client_fd,
                peer_addr,
                io_handler_,
                buffer_manager_,
                scheduler_,
                dispatcher_
            );
            
            conn->init();
            conn->SetConnectionId(connection_manager_->GetConnectionCount() + 1);
            connection_manager_->RegisterConnection(conn);
            
            onNewConnection(conn);
        }
    }
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
    if (protocol_handler_) {
        protocol_handler_->OnData(conn, data);
    } else {
        std::cerr << "protocol_handler_ is null!" << std::endl;
    }
}

void HttpServer::onError(std::shared_ptr<core::IConnection> conn, std::error_code ec) {
    std::cerr << "Connection error: " << ec.message() << std::endl;
    conn->Close();
}

void HttpServer::Listen(const std::string& host, uint16_t port) {
    host_ = host;
    port_ = port;
    
    if (!createListeningSocket(host, port)) {
        std::cerr << "Failed to start server" << std::endl;
        return;
    }

    // 将所有路由注册到 protocol_handler_
    // 1. API 路由
    protocol_handler_->Post("/api/record", [this](const HttpRequest& req, HttpResponse& resp) {
        handleSaveRecord(req, resp);
    });
    protocol_handler_->Get("/api/stats", [this](const HttpRequest& req, HttpResponse& resp) {
        handleGetStats(req, resp);
    });
    protocol_handler_->Get("/api/devices", [this](const HttpRequest& req, HttpResponse& resp) {
        handleGetDevices(req, resp);
    });
    protocol_handler_->Get("/api/history", [this](const HttpRequest& req, HttpResponse& resp) {
        handleGetHistory(req, resp);
    });
    protocol_handler_->Get("/admin", [this](const HttpRequest& req, HttpResponse& resp) {
        handleAdminPage(req, resp);
    });

    // 2. 静态文件服务（包含根路径 /）
    protocol_handler_->ServeStatic("/static", "../public");
    protocol_handler_->ServeStatic("/css", "../public/css");
    protocol_handler_->ServeStatic("/js", "../public/js");
    protocol_handler_->ServeStatic("/", "../public");  // 根路径也使用静态文件，会查找 index.html

    // 可选：注册一个简单的根路径处理器作为后备（如果静态文件找不到）
    protocol_handler_->Get("/", [this](const HttpRequest& req, HttpResponse& resp) {
        // 尝试读取 index.html（静态文件服务已处理，这里仅作后备）
        std::string content;
        std::vector<std::string> index_paths = {
            "../public/index.html",
            "./public/index.html",
            "/CODE/OrangeLi_Http_Server/examples/public/index.html"
        };
        for (const auto& p : index_paths) {
            if (readFile(p, content)) {
                resp.SetStatus(200);
                resp.SetBody(content);
                resp.SetContentType("text/html");
                return;
            }
        }
        resp.SetStatus(404);
        resp.SetBody("404 Not Found");
    });

    running_ = true;
    accept_thread_ = std::make_unique<std::thread>([this]() { acceptLoop(); });
    
    if (accept_thread_ && accept_thread_->joinable()) {
        accept_thread_->join();
    }

    std::cout << "\n🚀 HTTP server listening on " << host << ":" << port << std::endl;
    std::cout << "   📊 Admin panel: http://" << host << ":" << port << "/admin" << std::endl;
    std::cout << "   🏃 Speed test: http://" << host << ":" << port << "/" << std::endl;
    std::cout << "\n📋 Registered routes:" << std::endl;
    for (const auto& [path, _] : get_handlers_) {
        std::cout << "     GET  " << path << std::endl;
    }
    for (const auto& [path, _] : post_handlers_) {
        std::cout << "     POST " << path << std::endl;
    }
    std::cout << std::endl;
}

void HttpServer::Stop() {
    running_ = false;
    if (accept_thread_ && accept_thread_->joinable()) {
        accept_thread_->join();
    }
    if (listen_fd_ >= 0) {
        close(listen_fd_);
        listen_fd_ = -1;
    }
}

// ========== API 实现（保持不变，但不再需要 handleHttpRequest / handleWebSocketUpgrade） ==========

void HttpServer::handleSaveRecord(const HttpRequest& req, HttpResponse& resp) {
    std::string body = req.GetBody();
    SpeedRecord record;
    
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
    else record.device_id = "device_" + std::to_string(rand());
    
    std::string dev_name = findValue("device_name");
    if (!dev_name.empty()) record.device_name = dev_name;
    else record.device_name = "Unknown Device";
    
    record.latency_ms = 0;
    record.packet_loss = 0;
    record.total_bytes = static_cast<size_t>(record.speed_mbps * 1024 * 1024 / 8 * 10);
    record.test_duration = 10;
    
    if (db_.saveSpeedRecord(record)) {
        resp.SetBody("{\"status\":\"ok\",\"message\":\"Record saved\"}");
    } else {
        resp.SetStatus(500);
        resp.SetBody("{\"status\":\"error\",\"message\":\"Database error\"}");
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
    // 保持原有 HTML 内容不变（省略，实际使用完整 HTML）
    std::string html = R"delimiter(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>测速后台管理</title>
    <style>
        * { box-sizing: border-box; }
        body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background: #f0f2f5; margin: 0; padding: 20px; }
        .container { max-width: 1400px; margin: 0 auto; }
        h1 { color: #1a1a2e; }
        .stats-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 20px; margin-bottom: 30px; }
        .stat-card { background: white; border-radius: 12px; padding: 20px; box-shadow: 0 2px 8px rgba(0,0,0,0.1); text-align: center; }
        .stat-number { font-size: 36px; font-weight: bold; color: #667eea; }
        .stat-label { color: #666; margin-top: 8px; }
        .card { background: white; border-radius: 12px; padding: 20px; margin-bottom: 30px; box-shadow: 0 2px 8px rgba(0,0,0,0.1); }
        .card h3 { margin-top: 0; color: #333; }
        table { width: 100%; border-collapse: collapse; }
        th, td { padding: 12px; text-align: left; border-bottom: 1px solid #eee; }
        th { background: #667eea; color: white; }
        button { background: #667eea; color: white; border: none; padding: 8px 16px; border-radius: 6px; cursor: pointer; margin-top: 10px; }
        button:hover { background: #5a67d8; }
        .refresh-btn { margin-bottom: 15px; }
    </style>
    <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
</head>
<body>
<div class="container">
    <h1>📊 测速后台管理</h1>
    <div class="stats-grid" id="stats"></div>
    <div class="card">
        <h3>📱 设备列表</h3>
        <button onclick="loadDevices()" class="refresh-btn">刷新</button>
        <table id="devices-table">
            <thead><tr><th>设备名称</th><th>最后测试</th><th>测试次数</th><th>平均速度 (Mbps)</th></tr></thead>
            <tbody></tbody>
        </table>
    </div>
    <div class="card">
        <h3>📜 历史记录</h3>
        <button onclick="loadHistory()" class="refresh-btn">刷新</button>
        <table id="history-table">
            <thead><tr><th>时间</th><th>设备</th><th>速度 (Mbps)</th></tr></thead>
            <tbody></tbody>
        </table>
    </div>
</div>
<script>
    async function loadStats() {
        const res = await fetch('/api/stats');
        const stats = await res.json();
        document.getElementById('stats').innerHTML = 
            '<div class="stat-card"><div class="stat-number">' + stats.total_tests + '</div><div class="stat-label">总测试次数</div></div>' +
            '<div class="stat-card"><div class="stat-number">' + stats.avg_speed.toFixed(1) + '</div><div class="stat-label">平均速度 (Mbps)</div></div>' +
            '<div class="stat-card"><div class="stat-number">' + stats.max_speed.toFixed(1) + '</div><div class="stat-label">最高速度 (Mbps)</div></div>' +
            '<div class="stat-card"><div class="stat-number">' + stats.active_devices + '</div><div class="stat-label">活跃设备</div></div>' +
            '<div class="stat-card"><div class="stat-number">' + (stats.total_bytes / 1e6).toFixed(1) + '</div><div class="stat-label">总传输 (MB)</div></div>';
    }
    async function loadDevices() {
        const res = await fetch('/api/devices');
        const devices = await res.json();
        let html = '';
        for (const d of devices) {
            html += `<tr><td>${escapeHtml(d.device_name)}</td><td>${d.last_seen}</td><td>${d.total_tests}</td><td>${d.avg_speed.toFixed(1)}</td></tr>`;
        }
        document.querySelector('#devices-table tbody').innerHTML = html;
    }
    async function loadHistory() {
        const res = await fetch('/api/history');
        const records = await res.json();
        let html = '';
        for (const r of records) {
            html += `<tr><td>${r.created_at}</td><td>${escapeHtml(r.device_name)}</td><td>${r.speed_mbps.toFixed(1)}</td></tr>`;
        }
        document.querySelector('#history-table tbody').innerHTML = html;
    }
    function escapeHtml(str) {
        if (!str) return '';
        return str.replace(/[&<>]/g, function(m) {
            if (m === '&') return '&amp;';
            if (m === '<') return '&lt;';
            if (m === '>') return '&gt;';
            return m;
        });
    }
    loadStats();
    loadDevices();
    loadHistory();
    setInterval(loadStats, 5000);
</script>
</body>
</html>)delimiter";
    resp.SetBody(html);
    resp.SetContentType("text/html");
}

} // namespace httpserver::application::http
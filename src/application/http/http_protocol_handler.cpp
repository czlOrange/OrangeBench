// src/application/http/http_protocol_handler.cpp
#include "application/http/http_protocol_handler.hpp"
#include "application/http/http_request.hpp"
#include "application/http/http_response.hpp"
#include "application/http/http_parser.hpp"
#include "core/Event/event_dispatcher.hpp"
#include "core/Connection/connection_interface.hpp"
#include "application/websocket/websocket_session.hpp"
#include "application/websocket/websocket_frame.hpp"
#include <fstream>
#include <filesystem>
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <thread>
#include <chrono>
#include <iostream>
#include <cstring>    // 解决 strchr
#include <cstdio>     // 解决 strerror
#include <errno.h>    // 解决 errno
namespace fs = std::filesystem;
namespace httpserver::application::http {

// ============================================================================
// 构造与析构
// ============================================================================

HttpProtocolHandler::HttpProtocolHandler(std::shared_ptr<core::IEventDispatcher> dispatcher)
    : dispatcher_(std::move(dispatcher))
    , running_(false)
    , max_body_size_(10 * 1024 * 1024)
    , request_timeout_(std::chrono::seconds(30))
    , total_requests_(0) {
}

HttpProtocolHandler::~HttpProtocolHandler() {
    Stop();
}

void HttpProtocolHandler::Start() {
    if (running_) return;
    running_ = true;

    // 启动清理线程
    cleanup_thread_ = std::make_unique<std::thread>([this]() {
        while (running_) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            CleanupTimeoutConnections();
        }
    });
}

void HttpProtocolHandler::Stop() {
    running_ = false;
    if (cleanup_thread_ && cleanup_thread_->joinable()) {
        cleanup_thread_->join();
    }
}

// ============================================================================
// 路由注册
// ============================================================================

void HttpProtocolHandler::Get(const std::string& path, HttpHandler handler) {
    std::unique_lock lock(routes_mutex_);
    get_routes_.push_back(CreateRouteEntry(path, handler));
}

void HttpProtocolHandler::Post(const std::string& path, HttpHandler handler) {
    std::unique_lock lock(routes_mutex_);
    post_routes_.push_back(CreateRouteEntry(path, handler));
}

void HttpProtocolHandler::Put(const std::string& path, HttpHandler handler) {
    std::unique_lock lock(routes_mutex_);
    put_routes_.push_back(CreateRouteEntry(path, handler));
}

void HttpProtocolHandler::Delete(const std::string& path, HttpHandler handler) {
    std::unique_lock lock(routes_mutex_);
    delete_routes_.push_back(CreateRouteEntry(path, handler));
}

void HttpProtocolHandler::ServeStatic(const std::string& url_prefix, const std::string& root_dir) {
    static_configs_.push_back({url_prefix, root_dir});
}

void HttpProtocolHandler::AddMiddleware(Middleware middleware) {
    middlewares_.push_back(middleware);
}

// ============================================================================
// 核心数据处理
// ============================================================================

void HttpProtocolHandler::OnData(std::shared_ptr<core::IConnection> conn, std::string_view data) {
    std::cout << "🔥 OnData called, size=" << data.size() << std::endl;
    if (!conn || !running_) return;
    
    std::string request_str(data.data(), data.size());
    if (data.size() > 0) {
        std::string preview(data.data(), std::min(data.size(), (size_t)200));
        std::cout << "📋 Preview: " << preview << std::endl;
    }
    
    if (request_str.find("Upgrade: websocket") != std::string::npos) {
        std::cout << "🔌 WebSocket upgrade detected!" << std::endl;
        std::cout << "📋 Full request head:\n" << request_str.substr(0, 500) << std::endl;
        
        std::string key;
        size_t key_pos = request_str.find("Sec-WebSocket-Key:");
        if (key_pos != std::string::npos) {
            size_t start = key_pos + 19;
            size_t end = request_str.find("\r\n", start);
            if (end != std::string::npos) {
                key = request_str.substr(start, end - start);
                key.erase(0, key.find_first_not_of(" \t"));
                key.erase(key.find_last_not_of(" \t") + 1);
            }
        }
        std::cout << "🔑 Extracted key: [" << key << "]" << std::endl;
        
        if (!key.empty()) {
            std::string response = websocket::WebSocketSession::HandshakeResponse(key);
            conn->Send(response);
            std::cout << "✅ WebSocket handshake sent" << std::endl;
            
            // 启动测速数据发送线程
        std::thread([conn]() {
            std::cout << "📤 Data sending thread started" << std::endl;
            const size_t chunk_size = 64 * 1024;  // 64KB
            std::vector<uint8_t> test_data(chunk_size, 0xAA);
            auto start_time = std::chrono::steady_clock::now();
            const int duration_seconds = 10;
            
            while (true) {
                auto frame = websocket::FrameCodec::EncodeBinary(test_data);
                auto result = conn->Send(std::string_view(reinterpret_cast<const char*>(frame.data()), frame.size()));
                
                if (result.has_error()) {
                    std::error_code ec = result.error();
                    if (ec == std::errc::resource_unavailable_try_again || ec == std::errc::operation_would_block) {
                        // 发送缓冲区满，等待 1ms 后重试
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                        continue;  // 重试当前帧
                    } else {
                        std::cerr << "WebSocket send error: " << ec.message() << std::endl;
                        break;
                    }
                }
                
                // 检查是否达到持续时间
                auto elapsed = std::chrono::steady_clock::now() - start_time;
                if (elapsed >= std::chrono::seconds(duration_seconds)) {
                    break;
                }
                
                // 正常发送后不加 sleep，让 TCP 尽可能快发送
                // 如果担心 CPU 占用过高，可加极短延时，但会影响测速上限
                // std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            
            auto close_frame = websocket::FrameCodec::EncodeClose(1000, "Test finished");
            conn->Send(std::string_view(reinterpret_cast<const char*>(close_frame.data()), close_frame.size()));
            std::cout << "📊 Speed test data transmission completed" << std::endl;
        }).detach();
        } else {
            std::cout << "❌ WebSocket key is empty, cannot handshake!" << std::endl;
        }
        return;
    }
    
    // HTTP 解析逻辑（保持不变）
    uint64_t conn_id = conn->GetConnectionId();
    auto& ctx = GetContext(conn_id);
    ctx.last_activity = std::chrono::steady_clock::now();
    ctx.buffer.append(data.data(), data.size());
    
    while (!ctx.buffer.empty()) {
        HttpRequest req;
        if (!ParseHttpRequest(ctx, req)) {
            std::cout << "⚠️ ParseHttpRequest incomplete, waiting for more data" << std::endl;
            break;
        }
        std::cout << "✅ Parsed request: " << req.GetMethod() << " " << req.GetPath() << std::endl;
        total_requests_++;
        HandleRequest(conn, req);
        if (!ctx.is_keep_alive) {
            conn->Close();
            RemoveContext(conn_id);
            break;
        }
        ctx.Reset();
    }
}


void HttpProtocolHandler::handleWebSocketUpgrade(std::shared_ptr<core::IConnection> conn, const HttpRequest& req) {
    std::string key = req.GetHeader("Sec-WebSocket-Key");
    if (key.empty()) {
        conn->Close();
        return;
    }

    // 发送握手响应
    std::string response = websocket::WebSocketSession::HandshakeResponse(key);
    auto send_result = conn->Send(response);
    if (send_result.has_error()) {
        conn->Close();
        return;
    }

    std::cout << "🔌 WebSocket upgrade successful for connection " << conn->GetConnectionId() << std::endl;

    // 创建 WebSocket 会话
    auto ws_session = std::make_shared<websocket::WebSocketSession>(conn);

    // 设置消息回调（可自定义业务逻辑）
    ws_session->SetOnMessage([](const std::string& msg) {
        std::cout << "📨 WebSocket message: " << msg << std::endl;
    });
    ws_session->SetOnClose([](uint16_t code, const std::string& reason) {
        std::cout << "🔌 WebSocket closed: code=" << code << ", reason=" << reason << std::endl;
    });

    // 保存会话
    uint64_t conn_id = conn->GetConnectionId();
    {
        std::unique_lock lock(websocket_mutex_);
        websocket_sessions_[conn_id] = ws_session;
    }

    // 替换数据回调为 WebSocket 处理
    conn->SetDataCallback([ws_session](std::shared_ptr<core::IConnection> c, std::string_view data) {
        ws_session->OnData(data);
    });

    // 从 HTTP 上下文中移除
    RemoveContext(conn_id);

    // ========== 启动测速数据发送线程 ==========
    // 前端 index.html 通过 WebSocket 接收二进制数据来计算速度
    std::thread([fd = conn->GetFd()]() {
        const size_t chunk_size = 64 * 1024;  // 64KB
        std::vector<uint8_t> test_data(chunk_size, 0xAA);  // 填充测试数据
        auto start_time = std::chrono::steady_clock::now();
        const int duration_seconds = 10;  // 持续发送 10 秒

        while (true) {
            // 编码为 WebSocket 二进制帧
            auto frame = websocket::FrameCodec::EncodeBinary(test_data);
            ssize_t sent = send(fd, frame.data(), frame.size(), 0);
            if (sent < 0) {
                std::cerr << "WebSocket send error: " << strerror(errno) << std::endl;
                break;
            }

            // 检查是否达到持续时间
            auto elapsed = std::chrono::steady_clock::now() - start_time;
            if (elapsed >= std::chrono::seconds(duration_seconds)) {
                break;
            }

            // 极短延时，避免占满 CPU（实际可无延时，但为了稳定加 1ms）
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        // 发送关闭帧
        auto close_frame = websocket::FrameCodec::EncodeClose(1000, "Test finished");
        send(fd, close_frame.data(), close_frame.size(), 0);
        close(fd);
        std::cout << "📊 Speed test data transmission completed" << std::endl;
    }).detach();
}

std::shared_ptr<websocket::WebSocketSession> HttpProtocolHandler::GetWebSocketSession(uint64_t conn_id) {
    std::shared_lock lock(websocket_mutex_);
    auto it = websocket_sessions_.find(conn_id);
    return it != websocket_sessions_.end() ? it->second : nullptr;
}

void HttpProtocolHandler::OnError(std::shared_ptr<core::IConnection> conn, std::error_code ec) {
    if (!conn) return;

    uint64_t conn_id = conn->GetConnectionId();
    {
        std::unique_lock lock(websocket_mutex_);
        websocket_sessions_.erase(conn_id);
    }
    RemoveContext(conn_id);

    std::cerr << "Connection error: " << ec.message() << std::endl;
}

// ============================================================================
// HTTP 解析
// ============================================================================

bool HttpProtocolHandler::ParseHttpRequest(ConnectionHttpContext& ctx, HttpRequest& req) {
    // 查找请求结束标记（\r\n\r\n）
    size_t header_end = ctx.buffer.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        return false;
    }

    std::string header_part = ctx.buffer.substr(0, header_end);

    // 解析请求行
    size_t line_end = header_part.find("\r\n");
    if (line_end == std::string::npos) return false;

    std::string request_line = header_part.substr(0, line_end);
    std::istringstream line_stream(request_line);
    std::string method, path, version;

    if (!(line_stream >> method >> path >> version)) {
        return false;
    }

    req.SetMethod(method);
    req.SetPath(path);
    req.SetVersion(version);

    // 解析头部
    size_t pos = line_end + 2;
    while (pos < header_part.size()) {
        size_t header_line_end = header_part.find("\r\n", pos);
        if (header_line_end == std::string::npos) break;

        std::string header_line = header_part.substr(pos, header_line_end - pos);
        size_t colon_pos = header_line.find(':');
        if (colon_pos != std::string::npos) {
            std::string key = header_line.substr(0, colon_pos);
            std::string value = header_line.substr(colon_pos + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            req.AddHeader(key, value);
        }

        pos = header_line_end + 2;
    }

    // 获取 Content-Length
    std::string content_length_str = req.GetHeader("Content-Length");
    size_t content_length = content_length_str.empty() ? 0 : std::stoul(content_length_str);

    if (content_length > max_body_size_) return false;

    size_t body_start = header_end + 4;
    if (ctx.buffer.size() < body_start + content_length) return false;

    if (content_length > 0) {
        std::string body = ctx.buffer.substr(body_start, content_length);
        req.SetBody(body);
    }

    // 解析查询参数
    size_t query_pos = path.find('?');
    if (query_pos != std::string::npos) {
        std::string query_string = path.substr(query_pos + 1);
        path = path.substr(0, query_pos);
        req.SetPath(path);

        size_t start = 0;
        while (start < query_string.size()) {
            size_t eq_pos = query_string.find('=', start);
            size_t amp_pos = query_string.find('&', start);

            if (eq_pos != std::string::npos && (amp_pos == std::string::npos || eq_pos < amp_pos)) {
                std::string key = query_string.substr(start, eq_pos - start);
                std::string value;
                size_t value_end = (amp_pos == std::string::npos) ? query_string.size() : amp_pos;
                if (eq_pos + 1 < value_end) {
                    value = query_string.substr(eq_pos + 1, value_end - eq_pos - 1);
                }
                //req.AddQueryParam(key, HttpParser::UrlDecode(value));
                start = value_end + 1;
            } else {
                start = (amp_pos == std::string::npos) ? query_string.size() : amp_pos + 1;
            }
        }
    }

    // 解析表单数据
    if (req.GetMethod() == "POST" &&
        req.GetHeader("Content-Type").find("application/x-www-form-urlencoded") != std::string::npos) {
        req.ParseFormData();
    }

    // 判断 keep-alive
    std::string conn_header = req.GetHeader("Connection");
    ctx.is_keep_alive = (conn_header == "keep-alive" ||
                         (conn_header.empty() && version == "HTTP/1.1"));

    size_t consumed = body_start + content_length;
    ctx.buffer.erase(0, consumed);

    return true;
}

// ============================================================================
// 请求处理
// ============================================================================

void HttpProtocolHandler::HandleRequest(std::shared_ptr<core::IConnection> conn, const HttpRequest& req) {
std::cout << "📋 HandleRequest: " << req.GetMethod() << " " << req.GetPath() << std::endl;

    HttpResponse resp;

    HttpRequest mutable_req = req;
    if (!ExecuteMiddlewares(mutable_req, resp)) {
        SendResponse(conn, resp);
        return;
    }

    HttpHandler handler;
    std::unordered_map<std::string, std::string> path_params;
    bool found = false;
    const std::string& method = req.GetMethod();
    const std::string& path = req.GetPath();

    if (method == "GET") {
        found = MatchRoute(get_routes_, path, handler, path_params);
    } else if (method == "POST") {
        found = MatchRoute(post_routes_, path, handler, path_params);
    } else if (method == "PUT") {
        found = MatchRoute(put_routes_, path, handler, path_params);
    } else if (method == "DELETE") {
        found = MatchRoute(delete_routes_, path, handler, path_params);
    }

    if (!found && method == "GET") {
        for (const auto& config : static_configs_) {
            if (path.find(config.url_prefix) == 0) {
                if (HandleStaticFile(config.url_prefix, config.root_dir, path, resp)) {
                    found = true;
                    break;
                }
            }
        }
    }

    if (!found) {
        resp.SetStatus(404, "Not Found");
        resp.SetBody("404 - Page Not Found");
        SendResponse(conn, resp);
        return;
    }

    for (const auto& [key, value] : path_params) {
        conn->SetUserData("param:" + key, value);
    }

    handler(mutable_req, resp);
    SendResponse(conn, resp);
}

void HttpProtocolHandler::SendResponse(std::shared_ptr<core::IConnection> conn, const HttpResponse& resp) {
    std::string response_str = resp.ToString();
    conn->SendAsync(response_str);
}

// ============================================================================
// 路由匹配
// ============================================================================

HttpProtocolHandler::RouteEntry HttpProtocolHandler::CreateRouteEntry(const std::string& pattern, HttpHandler handler) {
    RouteEntry entry;
    entry.pattern = pattern;
    entry.handler = handler;

    std::string regex_pattern = "^";
    size_t pos = 0;
    while (pos < pattern.size()) {
        if (pattern[pos] == ':') {
            size_t end = pattern.find('/', pos);
            if (end == std::string::npos) end = pattern.size();
            std::string param_name = pattern.substr(pos + 1, end - pos - 1);
            entry.param_names.push_back(param_name);
            regex_pattern += "([^/]+)";
            pos = end;
        } else {
            if (strchr(".*+?^$|{}()", pattern[pos])) regex_pattern += '\\';
            regex_pattern += pattern[pos];
            ++pos;
        }
    }
    regex_pattern += "$";

    entry.regex = std::regex(regex_pattern, std::regex::ECMAScript);
    return entry;
}

bool HttpProtocolHandler::MatchRoute(const std::vector<RouteEntry>& routes,
                                      const std::string& path,
                                      HttpHandler& handler,
                                      std::unordered_map<std::string, std::string>& params) {
    std::shared_lock lock(routes_mutex_);

    for (const auto& route : routes) {
        std::smatch match;
        if (std::regex_match(path, match, route.regex)) {
            handler = route.handler;
            for (size_t i = 0; i < route.param_names.size() && i + 1 < match.size(); ++i) {
                params[route.param_names[i]] = match[i + 1];
            }
            return true;
        }
    }
    return false;
}

// ============================================================================
// 静态文件处理
// ============================================================================

bool HttpProtocolHandler::HandleStaticFile(const std::string& url_prefix,
                                            const std::string& root_dir,
                                            const std::string& path,
                                            HttpResponse& resp) {
    std::string relative_path = path.substr(url_prefix.size());
    if (relative_path.empty() || relative_path[0] != '/') {
        relative_path = "/" + relative_path;
    }

    if (relative_path.find("..") != std::string::npos) return false;

    fs::path file_path = fs::path(root_dir) / relative_path.substr(1);
    std::error_code ec;
    if (!fs::exists(file_path, ec) || !fs::is_regular_file(file_path, ec)) return false;

    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) return false;

    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());

    resp.SetBody(content);
    resp.SetContentType(GetMimeType(file_path.extension().string()));
    return true;
}

std::string HttpProtocolHandler::GetMimeType(const std::string& ext) {
    static const std::unordered_map<std::string, std::string> mime_types = {
        {".html", "text/html"}, {".htm", "text/html"},
        {".css", "text/css"}, {".js", "application/javascript"},
        {".json", "application/json"}, {".png", "image/png"},
        {".jpg", "image/jpeg"}, {".jpeg", "image/jpeg"},
        {".gif", "image/gif"}, {".svg", "image/svg+xml"},
        {".ico", "image/x-icon"}, {".txt", "text/plain"},
        {".xml", "application/xml"}, {".pdf", "application/pdf"},
        {".zip", "application/zip"}, {".mp3", "audio/mpeg"},
        {".mp4", "video/mp4"},
    };
    auto it = mime_types.find(ext);
    return it != mime_types.end() ? it->second : "application/octet-stream";
}

// ============================================================================
// 中间件
// ============================================================================

bool HttpProtocolHandler::ExecuteMiddlewares(HttpRequest& req, HttpResponse& resp) {
    for (const auto& middleware : middlewares_) {
        if (!middleware(req, resp)) return false;
    }
    return true;
}

// ============================================================================
// 连接上下文管理
// ============================================================================

ConnectionHttpContext& HttpProtocolHandler::GetContext(uint64_t conn_id) {
    std::unique_lock lock(contexts_mutex_);
    return contexts_[conn_id];
}

void HttpProtocolHandler::RemoveContext(uint64_t conn_id) {
    std::unique_lock lock(contexts_mutex_);
    contexts_.erase(conn_id);
}

void HttpProtocolHandler::CleanupTimeoutConnections() {
    auto now = std::chrono::steady_clock::now();
    std::unique_lock lock(contexts_mutex_);
    std::vector<uint64_t> timeout_conns;
    for (const auto& [conn_id, ctx] : contexts_) {
        if (now - ctx.last_activity > request_timeout_) {
            timeout_conns.push_back(conn_id);
        }
    }
    lock.unlock();
    for (uint64_t conn_id : timeout_conns) {
        RemoveContext(conn_id);
    }
}

HttpProtocolHandler::Stats HttpProtocolHandler::GetStats() const {
    Stats stats;
    stats.total_requests = total_requests_.load();
    stats.active_connections = contexts_.size();
    return stats;
}

} // namespace httpserver::application::http
// src/application/http/http_protocol_handler.cpp
#include "httpserver/application/http/http_protocol_handler.hpp"
#include "httpserver/application/http/http_request.hpp"
#include "httpserver/application/http/http_response.hpp"
#include "httpserver/application/websocket/speedtest_ws_handler.hpp"
#include "core/Event/event_dispatcher.hpp"
#include "core/Connection/connection_interface.hpp"
#include <fstream>
#include <filesystem>
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <thread>
#include <chrono>
#include <iostream>
#include <cstring>
#include <cstdio>
#include <errno.h>
#include "httpserver/application/websocket/speedtest_ws_handler.hpp"
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
    if (!conn || !running_) return;
    
    uint64_t conn_id = conn->GetConnectionId();
    auto& ctx = GetContext(conn_id);
    ctx.last_activity = std::chrono::steady_clock::now();
    ctx.buffer.append(data.data(), data.size());

    // 检查是否为 WebSocket 升级（仅当缓冲区包含完整请求头）
    size_t header_end = ctx.buffer.find("\r\n\r\n");
    if (header_end != std::string::npos) {
        std::string header_part = ctx.buffer.substr(0, header_end);
        
        // 检查是否是 WebSocket 升级请求
        if (header_part.find("Upgrade: websocket") != std::string::npos ||
            header_part.find("Upgrade: WebSocket") != std::string::npos) {
            
            // 使用临时上下文解析，避免破坏原始缓冲区
            ConnectionHttpContext temp_ctx = ctx;
            HttpRequest temp_req;
            
            if (ParseHttpRequest(temp_ctx, temp_req)) {
                std::string upgrade_header = temp_req.GetHeader("Upgrade");
                std::transform(upgrade_header.begin(), upgrade_header.end(), 
                               upgrade_header.begin(), ::tolower);
                
                // 在 OnData 函数中，WebSocket 升级处理部分
                if (upgrade_header == "websocket" && temp_req.GetPath() == "/ws/speedtest") {
                    std::cout << "🔌 WebSocket upgrade detected for conn " << conn_id << std::endl;
                    
                    // 计算需要消费的字节数
                    size_t content_length = 0;
                    std::string cl_str = temp_req.GetHeader("Content-Length");
                    if (!cl_str.empty()) content_length = std::stoul(cl_str);
                    size_t consumed = header_end + 4 + content_length;
                    
                    // 直接从原始缓冲区删除已解析部分
                    ctx.buffer.erase(0, consumed);
                    
                    // 获取 Sec-WebSocket-Key
                    std::string key = temp_req.GetHeader("Sec-WebSocket-Key");
                    if (!key.empty()) {
                        // 委托给专门的测速 WebSocket 处理器（注意命名空间）
                        //auto ws_session = websocket::SpeedTestWebSocketHandler::CreateAndHandle(conn, key, conn_id);
                        auto ws_session = httpserver::application::websocket::SpeedTestWebSocketHandler::CreateAndHandle(conn, key, conn_id);
                        if (ws_session) {
                            conn->SetDataCallback([ws_session](std::shared_ptr<core::IConnection> c, std::string_view d) {
                                ws_session->OnData(d);
                            });
                            RemoveContext(conn_id);
                            return;
                        }
                    }
                    // 握手失败，关闭连接
                    conn->Close();
                    RemoveContext(conn_id);
                    return;
                }
            }
        }
    }

    // 普通 HTTP 处理
    while (!ctx.buffer.empty()) {
        HttpRequest req;
        if (!ParseHttpRequest(ctx, req)) {
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

void HttpProtocolHandler::OnError(std::shared_ptr<core::IConnection> conn, std::error_code ec) {
    if (!conn) return;
    uint64_t conn_id = conn->GetConnectionId();
    RemoveContext(conn_id);
    std::cerr << "Connection error: " << ec.message() << std::endl;
}

// ============================================================================
// HTTP 解析（保持不变）
// ============================================================================

bool HttpProtocolHandler::ParseHttpRequest(ConnectionHttpContext& ctx, HttpRequest& req) {
    size_t header_end = ctx.buffer.find("\r\n\r\n");
    if (header_end == std::string::npos) return false;

    std::string header_part = ctx.buffer.substr(0, header_end);
    size_t line_end = header_part.find("\r\n");
    if (line_end == std::string::npos) return false;

    std::string request_line = header_part.substr(0, line_end);
    std::istringstream line_stream(request_line);
    std::string method, path, version;

    if (!(line_stream >> method >> path >> version)) return false;

    req.SetMethod(method);
    req.SetPath(path);
    req.SetVersion(version);

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

    std::string content_length_str = req.GetHeader("Content-Length");
    size_t content_length = content_length_str.empty() ? 0 : std::stoul(content_length_str);
    if (content_length > max_body_size_) return false;

    size_t body_start = header_end + 4;
    if (ctx.buffer.size() < body_start + content_length) return false;

    if (content_length > 0) {
        req.SetBody(ctx.buffer.substr(body_start, content_length));
    }

    size_t query_pos = path.find('?');
    if (query_pos != std::string::npos) {
        req.SetPath(path.substr(0, query_pos));
    }

    if (req.GetMethod() == "POST" &&
        req.GetHeader("Content-Type").find("application/x-www-form-urlencoded") != std::string::npos) {
        req.ParseFormData();
    }

    std::string conn_header = req.GetHeader("Connection");
    ctx.is_keep_alive = (conn_header == "keep-alive" ||
                         (conn_header.empty() && version == "HTTP/1.1"));

    ctx.buffer.erase(0, body_start + content_length);
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
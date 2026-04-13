#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <shared_mutex>
#include <regex>
#include <chrono>
#include <vector>
#include <atomic>
#include <thread>
#include "application/websocket/websocket_session.hpp"
namespace httpserver::core {
    class IEventDispatcher;
    class IConnection;
}

namespace httpserver::application::http {

class HttpRequest;
class HttpResponse;

using WebSocketSession = websocket::WebSocketSession;
using HttpHandler = std::function<void(const HttpRequest&, HttpResponse&)>;

// 每个连接的 HTTP 上下文
struct ConnectionHttpContext {
    std::string buffer;                                    // 未完成的数据
    std::chrono::steady_clock::time_point last_activity;  // 最后活动时间
    bool is_keep_alive = true;                            // 是否保持连接
    size_t content_length = 0;                             // 期望的 body 长度
    bool headers_parsed = false;                          // 头部是否解析完成
    
    void Reset() {
        buffer.clear();
        content_length = 0;
        headers_parsed = false;
        // is_keep_alive 保留，因为可能复用连接
    }
};

class HttpProtocolHandler {
public:
    explicit HttpProtocolHandler(std::shared_ptr<core::IEventDispatcher> dispatcher);
    ~HttpProtocolHandler();

    HttpProtocolHandler(const HttpProtocolHandler&) = delete;
    HttpProtocolHandler& operator=(const HttpProtocolHandler&) = delete;

    // 路由注册
    void Get(const std::string& path, HttpHandler handler);
    void Post(const std::string& path, HttpHandler handler);
    void Put(const std::string& path, HttpHandler handler);
    void Delete(const std::string& path, HttpHandler handler);
    
    // 静态文件服务
    void ServeStatic(const std::string& url_prefix, const std::string& root_dir);
    
    // 中间件
    using Middleware = std::function<bool(HttpRequest&, HttpResponse&)>;
    void AddMiddleware(Middleware middleware);
    
    // 配置
    void SetMaxBodySize(size_t size) { max_body_size_ = size; }
    void SetRequestTimeout(std::chrono::seconds timeout) { request_timeout_ = timeout; }
    
    // 启动/停止（订阅事件）
    void Start();
    void Stop();

    // 获取统计
    struct Stats {
        size_t total_requests{0};
        size_t active_connections{0};
    };
    Stats GetStats() const;
    
    //====================================================================
    //************************ websocket 相关接口 *************************
    //====================================================================

    // 新增：处理 WebSocket 升级请求
    void handleWebSocketUpgrade(std::shared_ptr<core::IConnection> conn, const HttpRequest& req);

    // 新增：获取 WebSocket 会话（可选）
    std::shared_ptr<WebSocketSession> GetWebSocketSession(uint64_t conn_id);

    void OnData(std::shared_ptr<core::IConnection> conn, std::string_view data);
private:
    // 事件回调（会被 dispatcher 调用）
    
    void OnError(std::shared_ptr<core::IConnection> conn, std::error_code ec);
    
    // 解析 HTTP 请求
    bool ParseHttpRequest(ConnectionHttpContext& ctx, HttpRequest& req);
    
    // 处理请求
    void HandleRequest(std::shared_ptr<core::IConnection> conn, const HttpRequest& req);
    
    // 发送响应
    void SendResponse(std::shared_ptr<core::IConnection> conn, const HttpResponse& resp);
    
    // 路由匹配
    struct RouteEntry {
        std::string pattern;
        std::regex regex;
        HttpHandler handler;
        std::vector<std::string> param_names;
    };
    
    RouteEntry CreateRouteEntry(const std::string& pattern, HttpHandler handler);
    bool MatchRoute(const std::vector<RouteEntry>& routes, 
                    const std::string& path,
                    HttpHandler& handler,
                    std::unordered_map<std::string, std::string>& params);
    
    // 静态文件处理
    bool HandleStaticFile(const std::string& url_prefix, 
                          const std::string& root_dir,
                          const std::string& path, 
                          HttpResponse& resp);
    
    // 执行中间件链
    bool ExecuteMiddlewares(HttpRequest& req, HttpResponse& resp);
    
    // 连接上下文管理
    ConnectionHttpContext& GetContext(uint64_t conn_id);
    void RemoveContext(uint64_t conn_id);
    
    // 清理超时连接
    void CleanupTimeoutConnections();
    
    // 获取 MIME 类型
    std::string GetMimeType(const std::string& path);
    
    std::shared_ptr<core::IEventDispatcher> dispatcher_;
    
    // 连接上下文映射
    std::unordered_map<uint64_t, ConnectionHttpContext> contexts_;
    std::shared_mutex contexts_mutex_;
    
    // 路由表
    std::vector<RouteEntry> get_routes_;
    std::vector<RouteEntry> post_routes_;
    std::vector<RouteEntry> put_routes_;
    std::vector<RouteEntry> delete_routes_;
    std::shared_mutex routes_mutex_;
    
    // 静态文件配置
    struct StaticConfig {
        std::string url_prefix;
        std::string root_dir;
    };
    std::vector<StaticConfig> static_configs_;
    
    // 中间件
    std::vector<Middleware> middlewares_;
    
    // 配置
    size_t max_body_size_ = 10 * 1024 * 1024;  // 10MB
    std::chrono::seconds request_timeout_{30};
    
    // 运行状态
    std::atomic<bool> running_{false};
    std::unique_ptr<std::thread> cleanup_thread_;
    
    // 统计
    std::atomic<size_t> total_requests_{0};

    // 新增：存储 WebSocket 会话（key: connection id）
    std::unordered_map<uint64_t, std::shared_ptr<WebSocketSession>> websocket_sessions_;
    std::shared_mutex websocket_mutex_;
};

} // namespace httpserver::application::http
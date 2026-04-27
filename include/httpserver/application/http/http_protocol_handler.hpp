// include/httpserver/application/http/http_protocol_handler.hpp
#pragma once

#include "http_request.hpp"
#include "http_response.hpp"
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <regex>
#include <shared_mutex>
#include <chrono>
#include <thread>
#include <atomic>

namespace httpserver::core {
    class IEventDispatcher;
    class IConnection;
}

namespace httpserver::application::http {

using HttpHandler = std::function<void(const HttpRequest&, HttpResponse&)>;
using Middleware = std::function<bool(HttpRequest&, HttpResponse&)>;

struct StaticFileConfig {
    std::string url_prefix;
    std::string root_dir;
};

struct ConnectionHttpContext {
    std::string buffer;
    std::chrono::steady_clock::time_point last_activity;
    bool is_keep_alive = true;

    void Reset() {
        is_keep_alive = true;
    }
};

class HttpProtocolHandler {
public:
    explicit HttpProtocolHandler(std::shared_ptr<core::IEventDispatcher> dispatcher);
    ~HttpProtocolHandler();

    void Start();
    void Stop();

    void Get(const std::string& path, HttpHandler handler);
    void Post(const std::string& path, HttpHandler handler);
    void Put(const std::string& path, HttpHandler handler);
    void Delete(const std::string& path, HttpHandler handler);
    void ServeStatic(const std::string& url_prefix, const std::string& root_dir);
    void AddMiddleware(std::function<bool(HttpRequest&, HttpResponse&)> middleware);

    void OnData(std::shared_ptr<core::IConnection> conn, std::string_view data);
    void OnError(std::shared_ptr<core::IConnection> conn, std::error_code ec);

    struct Stats {
        size_t total_requests;
        size_t active_connections;
    };
    Stats GetStats() const;

private:
    struct RouteEntry {
        std::string pattern;
        HttpHandler handler;
        std::regex regex;
        std::vector<std::string> param_names;
    };

    ConnectionHttpContext& GetContext(uint64_t conn_id);
    void RemoveContext(uint64_t conn_id);
    void CleanupTimeoutConnections();

    bool ParseHttpRequest(ConnectionHttpContext& ctx, HttpRequest& req);
    void HandleRequest(std::shared_ptr<core::IConnection> conn, const HttpRequest& req);
    void SendResponse(std::shared_ptr<core::IConnection> conn, const HttpResponse& resp);
    bool ExecuteMiddlewares(HttpRequest& req, HttpResponse& resp);

    RouteEntry CreateRouteEntry(const std::string& pattern, HttpHandler handler);
    bool MatchRoute(const std::vector<RouteEntry>& routes,
                    const std::string& path,
                    HttpHandler& handler,
                    std::unordered_map<std::string, std::string>& params);

    bool HandleStaticFile(const std::string& url_prefix,
                          const std::string& root_dir,
                          const std::string& path,
                          HttpResponse& resp);
    std::string GetMimeType(const std::string& ext);

    std::shared_ptr<core::IEventDispatcher> dispatcher_;
    std::atomic<bool> running_;
    size_t max_body_size_;
    std::chrono::milliseconds request_timeout_;
    std::atomic<size_t> total_requests_;

    std::vector<RouteEntry> get_routes_;
    std::vector<RouteEntry> post_routes_;
    std::vector<RouteEntry> put_routes_;
    std::vector<RouteEntry> delete_routes_;
    std::vector<StaticFileConfig> static_configs_;
    std::vector<std::function<bool(HttpRequest&, HttpResponse&)>> middlewares_;
    mutable std::shared_mutex routes_mutex_;

    std::unordered_map<uint64_t, ConnectionHttpContext> contexts_;
    mutable std::shared_mutex contexts_mutex_;
    std::unique_ptr<std::thread> cleanup_thread_;
};

} // namespace httpserver::application::http
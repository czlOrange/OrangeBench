// include/httpserver/application/http/http_server.hpp
#pragma once

#include "httpserver/application/http/http_request.hpp"
#include "httpserver/application/http/http_response.hpp"
#include "httpserver/application/http/http_parser.hpp"
#include "httpserver/infrastructure/database.hpp"
#include "httpserver/core/Event/event_dispatcher.hpp"
#include "httpserver/core/net/io_handler.hpp"
#include "httpserver/core/Buffer/buffer_manager.hpp"
#include "httpserver/core/Async/async_scheduler.hpp"
#include "httpserver/core/Connection/connection_manager.hpp"
#include "httpserver/application/http/http_protocol_handler.hpp"

#include <functional>
#include <memory>
#include <unordered_map>
#include <string>
#include <atomic>
#include <thread>
#include <vector>

namespace httpserver::application::http {

using HttpHandler = std::function<void(const HttpRequest&, HttpResponse&)>;

struct StaticFileConfig {
    std::string url_prefix;
    std::string root_dir;
};

class HttpServer {
public:
    explicit HttpServer(std::shared_ptr<core::IEventDispatcher> dispatcher);
    ~HttpServer();

    void Get(const std::string& path, HttpHandler handler);
    void Post(const std::string& path, HttpHandler handler);
    void ServeStatic(const std::string& url_prefix, const std::string& root_dir);
    void Listen(const std::string& host, uint16_t port);
    void Stop();

private:
    void acceptLoop();
    void onNewConnection(std::shared_ptr<core::IConnection> conn);
    void onData(std::shared_ptr<core::IConnection> conn, std::string_view data);
    void onError(std::shared_ptr<core::IConnection> conn, std::error_code ec);
    void handleHttpRequest(std::shared_ptr<core::IConnection> conn, const HttpRequest& req);
    void handleWebSocketUpgrade(std::shared_ptr<core::IConnection> conn, const std::string& request);
    bool createListeningSocket(const std::string& host, uint16_t port);

    // API 处理器
    void handleSaveRecord(const HttpRequest& req, HttpResponse& resp);
    void handleGetStats(const HttpRequest& req, HttpResponse& resp);
    void handleGetDevices(const HttpRequest& req, HttpResponse& resp);
    void handleGetHistory(const HttpRequest& req, HttpResponse& resp);
    void handleAdminPage(const HttpRequest& req, HttpResponse& resp);

    // 成员变量
    std::shared_ptr<core::IEventDispatcher> dispatcher_;
    std::shared_ptr<core::IIOHandler> io_handler_;
    std::shared_ptr<core::IBufferManager> buffer_manager_;
    std::shared_ptr<core::async::IScheduler> scheduler_;
    std::shared_ptr<core::IConnectionManager> connection_manager_;
    std::shared_ptr<HttpProtocolHandler> protocol_handler_;  // 添加这一行
    Database db_;

    std::unordered_map<std::string, HttpHandler> get_handlers_;
    std::unordered_map<std::string, HttpHandler> post_handlers_;
    std::vector<StaticFileConfig> static_configs_;

    int listen_fd_;
    std::atomic<bool> running_;
    std::unique_ptr<std::thread> accept_thread_;
    std::string host_;
    uint16_t port_;
};

} // namespace httpserver::application::http
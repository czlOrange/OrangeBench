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
#include "httpserver/application/http/handler/SpeedTestApi.hpp"
#include "httpserver/application/http/handler/AdminPageHandler.hpp"
#include <functional>
#include <memory>
#include <unordered_map>
#include <string>
#include <atomic>
#include <thread>
#include <vector>

namespace httpserver::application::http {

using HttpHandler = std::function<void(const HttpRequest&, HttpResponse&)>;

class HttpServer {
public:
    explicit HttpServer(std::shared_ptr<core::IEventDispatcher> dispatcher,std::shared_ptr<Database> database);
    ~HttpServer();

    void Get(const std::string& path, HttpHandler handler);
    void Post(const std::string& path, HttpHandler handler);
    void ServeStatic(const std::string& url_prefix, const std::string& root_dir);
    void Listen(const std::string& host, uint16_t port);
    void Stop();
    HttpProtocolHandler& GetProtocolHandler();
private:
    bool createListeningSocket(const std::string& host, uint16_t port);
    void acceptLoop();
    void onNewConnection(std::shared_ptr<core::IConnection> conn);
    void onData(std::shared_ptr<core::IConnection> conn, std::string_view data);
    void onError(std::shared_ptr<core::IConnection> conn, std::error_code ec);    
    void handleHttpRequest(std::shared_ptr<core::IConnection> conn, const HttpRequest& req);
    void handleWebSocketUpgrade(std::shared_ptr<core::IConnection> conn, const std::string& request);
    
    // 1. 核心组件（按依赖顺序）
    std::shared_ptr<core::IEventDispatcher> dispatcher_;
    std::shared_ptr<Database> database_;  
    std::shared_ptr<core::IIOHandler> io_handler_;
    std::shared_ptr<core::IBufferManager> buffer_manager_;
    std::shared_ptr<core::async::IScheduler> scheduler_;
    std::shared_ptr<core::IConnectionManager> connection_manager_;
    std::shared_ptr<HttpProtocolHandler> protocol_handler_;
    
    // 2. API 处理器（依赖 database_）
    SpeedTestApi speed_test_api_;
    AdminPageHandler admin_page_handler_;
    void handleSaveRecord(const HttpRequest& req, HttpResponse& resp);
    void handleGetStats(const HttpRequest& req, HttpResponse& resp);
    void handleGetDevices(const HttpRequest& req, HttpResponse& resp);
    void handleGetHistory(const HttpRequest& req, HttpResponse& resp);
    void handleAdminPage(const HttpRequest& req, HttpResponse& resp);
    
    // 3. 路由和状态
    std::unordered_map<std::string, HttpHandler> get_handlers_;
    std::unordered_map<std::string, HttpHandler> post_handlers_;
    std::vector<StaticFileConfig> static_configs_;
    
    // 4. 网络相关
    int listen_fd_;
    std::atomic<bool> running_;
    std::unique_ptr<std::thread> accept_thread_;
    std::string host_;
    uint16_t port_;

    
};

} // namespace httpserver::application::http
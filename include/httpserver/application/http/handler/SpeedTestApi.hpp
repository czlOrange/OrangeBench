#pragma once

#include "httpserver/application/http/http_protocol_handler.hpp"
#include "httpserver/application/http/http_request.hpp"
#include "httpserver/application/http/http_response.hpp"
#include "httpserver/infrastructure/database.hpp"

namespace httpserver::application::http {

class SpeedTestApi {
public:
    // 只保留这一个构造函数
    explicit SpeedTestApi(std::shared_ptr<Database> db);
    ~SpeedTestApi() = default;

    void Register(HttpProtocolHandler& handler);

private:
    void handleSaveRecord(const HttpRequest& req, HttpResponse& resp);
    void handleGetStats(const HttpRequest& req, HttpResponse& resp);
    void handleGetDevices(const HttpRequest& req, HttpResponse& resp);
    void handleGetHistory(const HttpRequest& req, HttpResponse& resp);
    void handleAdminPage(const HttpRequest& req, HttpResponse& resp);

    std::shared_ptr<Database> db_;
};

} // namespace httpserver::application::http
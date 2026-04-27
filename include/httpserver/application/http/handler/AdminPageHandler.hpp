// include/httpserver/application/http/handler/AdminPageHandler.hpp
#pragma once

#include "httpserver/application/http/http_protocol_handler.hpp"
#include "httpserver/application/http/http_response.hpp"
#include "httpserver/infrastructure/database.hpp"
namespace httpserver::application::http {

class AdminPageHandler {
public:
    void Register(HttpProtocolHandler& handler);
    explicit AdminPageHandler(std::shared_ptr<Database> db);  // 新增
private:
    std::shared_ptr<Database> db_;    
    void handleAdminPage(const HttpRequest& req, HttpResponse& resp);
};

} // namespace httpserver::application::http
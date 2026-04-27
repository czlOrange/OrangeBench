// include/httpserver/application/http/http_route_config.hpp
#pragma once

#include <memory>

namespace httpserver::application::http {

class HttpServer;
class Database;

class HttpRouteConfig {
public:
    // ✅ 修改：添加 Database 参数
    static void RegisterRoutes(HttpServer& server, std::shared_ptr<Database> database);
};

} // namespace httpserver::application::http
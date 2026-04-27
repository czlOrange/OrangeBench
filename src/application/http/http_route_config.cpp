// src/application/http/http_route_config.cpp
#include "httpserver/application/http/http_route_config.hpp"
#include "httpserver/application/http/http_server.hpp"
#include "httpserver/application/http/handler/SpeedTestApi.hpp"
#include "httpserver/application/http/handler/AdminPageHandler.hpp"
#include "httpserver/infrastructure/database.hpp"  // ✅ 添加这个头文件
#include <iostream>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace httpserver::application::http {

void HttpRouteConfig::RegisterRoutes(HttpServer& server, std::shared_ptr<Database> database) {
    auto& handler = server.GetProtocolHandler();
    // ✅ 检查 database 是否为空
    if (!database) {
        std::cerr << "[HttpRouteConfig] ERROR: Database is null!" << std::endl;
        return;
    }
    
    // ✅ 业务路由：传递 database 参数
    SpeedTestApi speed_test_api(database);
    speed_test_api.Register(handler);
    
    AdminPageHandler admin_handler(database);
    admin_handler.Register(handler);

    // 方式1：静态文件服务
    handler.ServeStatic("/static", "examples/public");
    handler.ServeStatic("/css", "examples/public/css");
    handler.ServeStatic("/js", "examples/public/js");
    
    // 方式2：直接处理根路径（因为 ServeStatic 可能不匹配 / 路径）
    handler.Get("/", [](const HttpRequest& req, HttpResponse& resp) {
        std::string content;
        std::vector<std::string> paths = {
            "examples/public/index.html",
            "/CODE/OrangeLi_Http_Server/examples/public/index.html"
        };
        
        for (const auto& p : paths) {
            std::ifstream file(p);
            if (file.is_open()) {
                content.assign((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
                break;
            }
        }
        
        if (content.empty()) {
            content = R"(
<!DOCTYPE html>
<html>
<head><title>Speed Test</title></head>
<body>
<h1>Speed Test Server</h1>
<p>Server is running! Please ensure index.html exists in examples/public/</p>
<a href="/admin">Admin Panel</a>
</body>
</html>
)";
        }
        
        resp.SetBody(content);
        resp.SetContentType("text/html");
    });
    
    std::cout << "📁 Static files served from: examples/public" << std::endl;
}

} // namespace httpserver::application::http
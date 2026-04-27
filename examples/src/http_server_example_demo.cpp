// examples/src/http_server_example_demo.cpp
#include "httpserver/application/http/http_server.hpp"
#include "httpserver/application/http/http_route_config.hpp"
#include "httpserver/infrastructure/database.hpp"  // ✅ 添加：Database 头文件
#include "httpserver/core/Event/event_dispatcher.hpp"
#include <iostream>
#include <memory>  // ✅ 添加：shared_ptr 需要这个头文件
#include <thread>

int main() {
    // 1. 创建核心组件
    auto dispatcher = httpserver::core::IEventDispatcher::CreateDefault();
    
    // ✅ 修改：明确类型并检查初始化
    auto db = std::make_shared<httpserver::application::http::Database>();
    if (!db->init()) {
        std::cerr << "❌ Failed to initialize database!" << std::endl;
        return 1;
    }
    std::cout << "✅ Database initialized successfully" << std::endl;
    
    // 2. 创建服务器（传递 dispatcher 和 db）
    httpserver::application::http::HttpServer server(dispatcher, db);

    // 3. 统一注册所有路由（传递 db）
    httpserver::application::http::HttpRouteConfig::RegisterRoutes(server, db);

    // 4. 启动服务器
    server.Listen("0.0.0.0", 8080);

    // 5. 保持程序运行
    std::cout << "Press Enter to stop..." << std::endl;
    std::cin.get();

    server.Stop();
    return 0;
}
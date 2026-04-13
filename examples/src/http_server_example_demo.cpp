// examples/src/http_server_example_demo.cpp
#include "httpserver/application/http/http_server.hpp"
#include "httpserver/core/Event/event_dispatcher.hpp"
#include <iostream>

int main() {
    auto dispatcher = httpserver::core::IEventDispatcher::CreateDefault();
    httpserver::application::http::HttpServer server(dispatcher);
    
    server.Get("/", [](const auto& req, auto& resp) {
        resp.SetBody("Hello, World!");
        resp.SetContentType("text/plain");
    });
    
    server.Listen("0.0.0.0", 8080);
    // ✅ 添加这行，防止程序退出
    std::cout << "Press Enter to stop..." << std::endl;
    std::cin.get();
    return 0;
}
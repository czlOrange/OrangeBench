// examples/src/http_server_example_demo.cpp
#include "httpserver/application/http/http_server.hpp"
#include <iostream>

using namespace httpserver::application::http;

int main() {
    auto dispatcher = httpserver::core::IEventDispatcher::CreateDefault();
    HttpServer server(dispatcher);

    server.ServeStatic("/", "../public");  // 改为 ../public

    // GET 路由（注释掉 / 路由，让静态文件优先）
    // server.Get("/", [](const HttpRequest& req, HttpResponse& resp) {
    //     resp.SetBody("Hello, World!\n");
    // });

    server.Get("/hello", [](const HttpRequest& req, HttpResponse& resp) {
        resp.SetBody("Hello from /hello\n");
    });

    server.Get("/json", [](const HttpRequest& req, HttpResponse& resp) {
        resp.SetBody("{\"message\": \"Hello JSON\"}");
        resp.SetContentType("application/json");
    });

    server.Post("/api/users", [](const HttpRequest& req, HttpResponse& resp) {
        std::string name = req.GetFormValue("name");
        std::string age = req.GetFormValue("age");
        std::string email = req.GetFormValue("email");
        
        std::string result = "User created:\n";
        result += "  name: " + name + "\n";
        result += "  age: " + age + "\n";
        result += "  email: " + email + "\n";
        
        resp.SetBody(result);
    });

    server.Post("/echo", [](const HttpRequest& req, HttpResponse& resp) {
        std::string body = req.GetBody();
        resp.SetBody("Echo: " + body);
    });

    server.Post("/api/data", [](const HttpRequest& req, HttpResponse& resp) {
        resp.SetBody("{\"status\": \"success\", \"received\": \"" + req.GetBody() + "\"}");
        resp.SetContentType("application/json");
    });

    server.Listen("0.0.0.0", 8080);

    std::cout << "\n✅ Server is ready. Press Enter to exit.\n";
    std::cin.get();

    return 0;
}
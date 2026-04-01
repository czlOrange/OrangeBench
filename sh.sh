#!/bin/bash
# /CODE/OrangeLi_Http_Server/add_post_support.sh

set -e

PROJECT_ROOT="/CODE/OrangeLi_Http_Server"
INCLUDE_DIR="$PROJECT_ROOT/include/httpserver/application/http"
SRC_DIR="$PROJECT_ROOT/src/application/http"
EXAMPLES_DIR="$PROJECT_ROOT/examples"

echo "========================================"
echo "   添加 POST 方法支持"
echo "========================================"

# ============================================================================
# 1. 修改 http_request.hpp（添加 body 和表单方法）
# ============================================================================
echo ""
echo "📝 修改 http_request.hpp..."

cat > "$INCLUDE_DIR/http_request.hpp" << 'EOF'
#pragma once

#include <string>
#include <unordered_map>

namespace httpserver::application::http {

class HttpRequest {
public:
    HttpRequest() = default;

    // 请求行
    void SetMethod(const std::string& method) { method_ = method; }
    void SetPath(const std::string& path) { path_ = path; }
    void SetVersion(const std::string& version) { version_ = version; }
    
    const std::string& GetMethod() const { return method_; }
    const std::string& GetPath() const { return path_; }
    const std::string& GetVersion() const { return version_; }

    // 头部
    void AddHeader(const std::string& key, const std::string& value) { headers_[key] = value; }
    const std::unordered_map<std::string, std::string>& GetHeaders() const { return headers_; }
    std::string GetHeader(const std::string& key) const {
        auto it = headers_.find(key);
        return it != headers_.end() ? it->second : "";
    }

    // 请求体（新增）
    void SetBody(const std::string& body) { body_ = body; }
    const std::string& GetBody() const { return body_; }
    
    // 表单数据解析（新增）
    void ParseFormData();
    std::string GetFormValue(const std::string& key) const;
    const std::unordered_map<std::string, std::string>& GetFormData() const { return form_data_; }
    bool HasFormData() const { return !form_data_.empty(); }

private:
    std::string method_;
    std::string path_;
    std::string version_;
    std::unordered_map<std::string, std::string> headers_;
    std::string body_;
    std::unordered_map<std::string, std::string> form_data_;  // 缓存的表单数据
};

} // namespace httpserver::application::http
EOF
echo "   ✅ http_request.hpp"

# ============================================================================
# 2. 修改 http_request.cpp（实现表单解析）
# ============================================================================
echo ""
echo "📝 修改 http_request.cpp..."

cat > "$SRC_DIR/http_request.cpp" << 'EOF'
#include "httpserver/application/http/http_request.hpp"
#include <sstream>
#include <algorithm>
#include <cctype>

namespace httpserver::application::http {

// URL 解码函数
static std::string urlDecode(const std::string& str) {
    std::string result;
    for (size_t i = 0; i < str.size(); ++i) {
        if (str[i] == '%' && i + 2 < str.size()) {
            // 解码 %XX
            char hex[3] = {str[i+1], str[i+2], 0};
            char* endptr;
            long val = strtol(hex, &endptr, 16);
            if (endptr == hex + 2) {
                result += static_cast<char>(val);
                i += 2;
                continue;
            }
        } else if (str[i] == '+') {
            result += ' ';
            continue;
        }
        result += str[i];
    }
    return result;
}

void HttpRequest::ParseFormData() {
    // 检查 Content-Type 是否为 application/x-www-form-urlencoded
    std::string content_type = GetHeader("Content-Type");
    if (content_type.find("application/x-www-form-urlencoded") == std::string::npos) {
        return;
    }
    
    form_data_.clear();
    std::string body = body_;
    
    // 按 & 分割
    size_t start = 0;
    size_t end = body.find('&');
    
    while (true) {
        std::string pair;
        if (end == std::string::npos) {
            pair = body.substr(start);
        } else {
            pair = body.substr(start, end - start);
        }
        
        // 按 = 分割
        size_t eq_pos = pair.find('=');
        if (eq_pos != std::string::npos) {
            std::string key = pair.substr(0, eq_pos);
            std::string value = pair.substr(eq_pos + 1);
            form_data_[urlDecode(key)] = urlDecode(value);
        }
        
        if (end == std::string::npos) break;
        start = end + 1;
        end = body.find('&', start);
    }
}

std::string HttpRequest::GetFormValue(const std::string& key) const {
    auto it = form_data_.find(key);
    return it != form_data_.end() ? it->second : "";
}

} // namespace httpserver::application::http
EOF
echo "   ✅ http_request.cpp"

# ============================================================================
# 3. 修改 http_parser.hpp（添加请求体解析）
# ============================================================================
echo ""
echo "📝 修改 http_parser.hpp..."

cat > "$INCLUDE_DIR/http_parser.hpp" << 'EOF'
#pragma once

#include "http_request.hpp"
#include <string>

namespace httpserver::application::http {

class HttpParser {
public:
    static int Parse(const std::string& data, HttpRequest& request);
};

} // namespace httpserver::application::http
EOF
echo "   ✅ http_parser.hpp"

# ============================================================================
# 4. 修改 http_parser.cpp（实现请求体解析）
# ============================================================================
echo ""
echo "📝 修改 http_parser.cpp..."

cat > "$SRC_DIR/http_parser.cpp" << 'EOF'
#include "httpserver/application/http/http_parser.hpp"
#include <sstream>
#include <algorithm>
#include <cctype>

namespace httpserver::application::http {

int HttpParser::Parse(const std::string& data, HttpRequest& request) {
    // 查找请求行结束位置（\r\n）
    size_t line_end = data.find("\r\n");
    if (line_end == std::string::npos) {
        return 0; // 数据不完整，等待更多数据
    }

    // 解析请求行: "GET /path HTTP/1.1"
    std::string request_line = data.substr(0, line_end);
    std::istringstream iss(request_line);
    std::string method, path, version;
    if (!(iss >> method >> path >> version)) {
        return -1; // 解析失败
    }

    request.SetMethod(method);
    request.SetPath(path);
    request.SetVersion(version);

    // 解析头部
    size_t pos = line_end + 2;
    size_t header_end = data.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        return 0; // 头部未完整
    }

    while (pos < header_end) {
        size_t line_end2 = data.find("\r\n", pos);
        if (line_end2 == std::string::npos) break;
        std::string header_line = data.substr(pos, line_end2 - pos);
        size_t colon = header_line.find(':');
        if (colon != std::string::npos) {
            std::string key = header_line.substr(0, colon);
            std::string value = header_line.substr(colon + 1);
            // 去除前导空格
            value.erase(0, value.find_first_not_of(" \t"));
            request.AddHeader(key, value);
        }
        pos = line_end2 + 2;
    }

    // 解析请求体
    size_t body_start = header_end + 4;
    if (body_start < data.size()) {
        std::string content_length_str = request.GetHeader("Content-Length");
        if (!content_length_str.empty()) {
            size_t content_length = std::stoul(content_length_str);
            if (body_start + content_length <= data.size()) {
                std::string body = data.substr(body_start, content_length);
                request.SetBody(body);
                
                // 如果是表单数据，自动解析
                if (request.GetHeader("Content-Type").find("application/x-www-form-urlencoded") != std::string::npos) {
                    const_cast<HttpRequest&>(request).ParseFormData();
                }
            }
        }
    }

    return header_end + 4;
}

} // namespace httpserver::application::http
EOF
echo "   ✅ http_parser.cpp"

# ============================================================================
# 5. 修改 http_server.hpp（添加 POST 路由）
# ============================================================================
echo ""
echo "📝 修改 http_server.hpp..."

cat > "$INCLUDE_DIR/http_server.hpp" << 'EOF'
#pragma once

#include "httpserver/core/tcp_connections/Event/event_dispatcher.hpp"
#include "httpserver/core/tcp_connections/IO/io_handler.hpp"
#include "httpserver/application/http/http_request.hpp"
#include "httpserver/application/http/http_response.hpp"
#include <functional>
#include <memory>
#include <unordered_map>
#include <string>
#include <atomic>

namespace httpserver::application::http {

using HttpHandler = std::function<void(const HttpRequest&, HttpResponse&)>;

class HttpServer {
public:
    explicit HttpServer(std::shared_ptr<core::IEventDispatcher> dispatcher);
    ~HttpServer();

    // 路由注册
    void Get(const std::string& path, HttpHandler handler);
    void Post(const std::string& path, HttpHandler handler);  // 新增 POST 路由

    void Listen(const std::string& host, uint16_t port);

private:
    void onNewConnection(std::shared_ptr<core::IConnection> conn);
    void onData(std::shared_ptr<core::IConnection> conn, std::string_view data);
    void onError(std::shared_ptr<core::IConnection> conn, std::error_code ec);
    
    bool createListeningSocket(const std::string& host, uint16_t port);
    void acceptLoop();

    std::shared_ptr<core::IEventDispatcher> dispatcher_;
    std::shared_ptr<core::IIOHandler> io_handler_;
    std::unordered_map<std::string, HttpHandler> get_handlers_;
    std::unordered_map<std::string, HttpHandler> post_handlers_;  // 新增 POST 路由表
    
    int listen_fd_;
    std::atomic<bool> running_;
};

} // namespace httpserver::application::http
EOF
echo "   ✅ http_server.hpp"

# ============================================================================
# 6. 修改 http_server.cpp（实现 POST 路由分发）
# ============================================================================
echo ""
echo "📝 修改 http_server.cpp..."

cat > "$SRC_DIR/http_server.cpp" << 'EOF'
#include "httpserver/application/http/http_server.hpp"
#include "httpserver/application/http/http_parser.hpp"
#include "httpserver/application/http/http_response.hpp"
#include "httpserver/core/tcp_connections/Connection/connection_interface.hpp"
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <thread>
#include <chrono>

namespace httpserver::application::http {

HttpServer::HttpServer(std::shared_ptr<core::IEventDispatcher> dispatcher)
    : dispatcher_(std::move(dispatcher))
    , io_handler_(core::IIOHandler::CreateDefault())
    , listen_fd_(-1)
    , running_(false) {}

HttpServer::~HttpServer() {
    if (listen_fd_ >= 0) {
        close(listen_fd_);
    }
}

void HttpServer::Get(const std::string& path, HttpHandler handler) {
    get_handlers_[path] = std::move(handler);
}

void HttpServer::Post(const std::string& path, HttpHandler handler) {
    post_handlers_[path] = std::move(handler);
}

bool HttpServer::createListeningSocket(const std::string& host, uint16_t port) {
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        std::cerr << "Failed to create socket: " << strerror(errno) << std::endl;
        return false;
    }

    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    int flags = fcntl(listen_fd_, F_GETFL, 0);
    fcntl(listen_fd_, F_SETFL, flags | O_NONBLOCK);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    
    if (host == "0.0.0.0") {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
    }

    if (bind(listen_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "Failed to bind: " << strerror(errno) << std::endl;
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    if (listen(listen_fd_, SOMAXCONN) < 0) {
        std::cerr << "Failed to listen: " << strerror(errno) << std::endl;
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    return true;
}

void HttpServer::acceptLoop() {
    while (running_) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(listen_fd_, (struct sockaddr*)&client_addr, &client_len);
        
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            continue;
        }

        int flags = fcntl(client_fd, F_GETFL, 0);
        fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);

        char buffer[8192];
        ssize_t n = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        if (n > 0) {
            buffer[n] = '\0';
            HttpRequest request;
            int parsed = HttpParser::Parse(std::string(buffer, n), request);
            
            HttpResponse response;
            if (parsed > 0) {
                std::string method = request.GetMethod();
                std::string path = request.GetPath();
                
                auto it = get_handlers_.find(path);
                if (method == "GET" && it != get_handlers_.end()) {
                    it->second(request, response);
                } else if (method == "POST") {
                    auto post_it = post_handlers_.find(path);
                    if (post_it != post_handlers_.end()) {
                        post_it->second(request, response);
                    } else {
                        response.SetStatus(404);
                        response.SetBody("Not Found");
                    }
                } else {
                    response.SetStatus(405);
                    response.SetBody("Method Not Allowed");
                }
            } else {
                response.SetStatus(400);
                response.SetBody("Bad Request");
            }
            
            std::string resp_str = response.ToString();
            send(client_fd, resp_str.c_str(), resp_str.size(), 0);
        }
        close(client_fd);
    }
}

void HttpServer::Listen(const std::string& host, uint16_t port) {
    if (!createListeningSocket(host, port)) {
        std::cerr << "Failed to start server" << std::endl;
        return;
    }

    running_ = true;
    std::cout << "✅ HTTP server listening on " << host << ":" << port << std::endl;
    std::cout << "   GET routes:" << std::endl;
    for (const auto& [path, _] : get_handlers_) {
        std::cout << "     - GET " << path << std::endl;
    }
    std::cout << "   POST routes:" << std::endl;
    for (const auto& [path, _] : post_handlers_) {
        std::cout << "     - POST " << path << std::endl;
    }
    std::cout << std::endl;

    acceptLoop();
}

void HttpServer::onNewConnection(std::shared_ptr<core::IConnection> conn) {
    conn->SetDataCallback([this](std::shared_ptr<core::IConnection> c, std::string_view data) {
        onData(c, data);
    });
    conn->SetErrorCallback([this](std::shared_ptr<core::IConnection> c, std::error_code ec) {
        onError(c, ec);
    });
}

void HttpServer::onData(std::shared_ptr<core::IConnection> conn, std::string_view data) {
    HttpRequest request;
    int parsed = HttpParser::Parse(std::string(data), request);
    if (parsed < 0) {
        HttpResponse resp;
        resp.SetStatus(400);
        resp.SetBody("Bad Request");
        conn->Send(resp.ToString());
        conn->Close();
        return;
    }
    if (parsed == 0) {
        return;
    }

    std::string method = request.GetMethod();
    std::string path = request.GetPath();
    HttpResponse response;
    
    auto get_it = get_handlers_.find(path);
    auto post_it = post_handlers_.find(path);
    
    if (method == "GET" && get_it != get_handlers_.end()) {
        get_it->second(request, response);
    } else if (method == "POST" && post_it != post_handlers_.end()) {
        post_it->second(request, response);
    } else if (method == "GET") {
        response.SetStatus(404);
        response.SetBody("Not Found");
    } else if (method == "POST") {
        response.SetStatus(404);
        response.SetBody("Not Found");
    } else {
        response.SetStatus(405);
        response.SetBody("Method Not Allowed");
    }
    
    response.SetContentType("text/plain");
    conn->Send(response.ToString());
    conn->Close();
}

void HttpServer::onError(std::shared_ptr<core::IConnection> conn, std::error_code ec) {
    std::cerr << "Connection error: " << ec.message() << std::endl;
    conn->Close();
}

} // namespace httpserver::application::http
EOF
echo "   ✅ http_server.cpp"

# ============================================================================
# 7. 创建示例程序（包含 POST 示例）
# ============================================================================
echo ""
echo "📝 创建示例程序..."

cat > "$EXAMPLES_DIR/http_server_example.cpp" << 'EOF'
#include "httpserver/application/http/http_server.hpp"
#include <iostream>

using namespace httpserver::application::http;

int main() {
    auto dispatcher = httpserver::core::IEventDispatcher::CreateDefault();
    HttpServer server(dispatcher);

    // GET 路由
    server.Get("/", [](const HttpRequest& req, HttpResponse& resp) {
        resp.SetBody("Hello, World!\n");
    });

    server.Get("/hello", [](const HttpRequest& req, HttpResponse& resp) {
        resp.SetBody("Hello from /hello\n");
    });

    server.Get("/json", [](const HttpRequest& req, HttpResponse& resp) {
        resp.SetBody("{\"message\": \"Hello JSON\"}");
        resp.SetContentType("application/json");
    });

    // POST 路由 - 接收表单数据
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

    // POST 路由 - 回声服务（返回原始请求体）
    server.Post("/echo", [](const HttpRequest& req, HttpResponse& resp) {
        std::string body = req.GetBody();
        resp.SetBody("Echo: " + body);
    });

    // POST 路由 - JSON 示例（简单返回）
    server.Post("/api/data", [](const HttpRequest& req, HttpResponse& resp) {
        resp.SetBody("{\"status\": \"success\", \"received\": \"" + req.GetBody() + "\"}");
        resp.SetContentType("application/json");
    });

    server.Listen("0.0.0.0", 8080);

    std::cout << "\n✅ Server is ready. Press Enter to exit.\n";
    std::cin.get();

    return 0;
}
EOF
echo "   ✅ http_server_example.cpp"

# ============================================================================
# 8. 更新编译脚本
# ============================================================================
echo ""
echo "📝 更新编译脚本..."

cat > "$EXAMPLES_DIR/build_http_server.sh" << 'EOF'
#!/bin/bash

echo "🔨 编译 HTTP 服务器示例..."

g++ -std=c++17 \
    -I../include \
    http_server_example.cpp \
    ../src/application/http/http_server.cpp \
    ../src/application/http/http_parser.cpp \
    ../src/application/http/http_response.cpp \
    ../src/application/http/http_request.cpp \
    -lpthread \
    -o http_server_example

if [ $? -eq 0 ]; then
    echo "✅ 编译成功！"
    echo "🚀 运行: ./http_server_example"
else
    echo "❌ 编译失败！"
    exit 1
fi
EOF

chmod +x "$EXAMPLES_DIR/build_http_server.sh"
echo "   ✅ build_http_server.sh"

# ============================================================================
# 完成
# ============================================================================
echo ""
echo "========================================"
echo "✅ POST 方法支持添加完成！"
echo "========================================"
echo ""
echo "📋 新增功能："
echo "   - POST 路由注册"
echo "   - 请求体解析"
echo "   - 表单数据解析（application/x-www-form-urlencoded）"
echo "   - URL 解码"
echo ""
echo "🚀 测试命令："
echo "   cd $EXAMPLES_DIR"
echo "   ./build_http_server.sh"
echo "   ./http_server_example"
echo ""
echo "📝 测试 POST 请求："
echo "   curl -X POST -d 'name=John&age=25&email=john@example.com' http://localhost:8080/api/users"
echo "   curl -X POST -d 'Hello World' http://localhost:8080/echo"
echo "   curl -X POST -d '{\"key\":\"value\"}' http://localhost:8080/api/data"
echo ""
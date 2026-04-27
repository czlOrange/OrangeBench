// src/application/http/http_server.cpp
#include "httpserver/application/http/http_server.hpp"
#include "httpserver/core/Tcp/tcp_connection.hpp"
#include "httpserver/application/http/http_protocol_handler.hpp"
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <memory>

namespace httpserver::application::http {

// 构造函数：初始化核心组件并启动调度器与协议处理器
HttpServer::HttpServer(std::shared_ptr<core::IEventDispatcher> dispatcher,std::shared_ptr<Database> database)
    : dispatcher_(std::move(dispatcher))
    , database_(std::move(database))
    , speed_test_api_(database_)
    , admin_page_handler_(database_)
    , io_handler_(core::IIOHandler::CreateDefault())
    , buffer_manager_(core::IBufferManager::CreateDefault())
    , scheduler_(core::async::IScheduler::CreateDefault())
    , connection_manager_(std::make_shared<core::DefaultConnectionManager>())
    , protocol_handler_(std::make_shared<HttpProtocolHandler>(dispatcher_))
    , listen_fd_(-1)
    , running_(false)
{
    if (scheduler_) {
        scheduler_->Start();
    }
    protocol_handler_->Start();
}

// 析构函数：停止服务并释放监听套接字
HttpServer::~HttpServer() {
    Stop();
    if (listen_fd_ >= 0) {
        close(listen_fd_);
    }
}

// 获取协议处理器，用于外部注册路由
HttpProtocolHandler& HttpServer::GetProtocolHandler() {
    return *protocol_handler_;
}

// 注册GET请求处理函数
void HttpServer::Get(const std::string& path, HttpHandler handler) {
    protocol_handler_->Get(path, std::move(handler));
}

// 注册POST请求处理函数
void HttpServer::Post(const std::string& path, HttpHandler handler) {
    protocol_handler_->Post(path, std::move(handler));
}

// 注册静态文件服务路径
void HttpServer::ServeStatic(const std::string& url_prefix, const std::string& root_dir) {
    protocol_handler_->ServeStatic(url_prefix, root_dir);
}

// 创建并配置监听套接字（socket、bind、listen）
bool HttpServer::createListeningSocket(const std::string& host, uint16_t port) {
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
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
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    if (listen(listen_fd_, SOMAXCONN) < 0) {
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    return true;
}

// 接收客户端连接的循环线程函数
void HttpServer::acceptLoop() {
    if (listen_fd_ < 0) {
        return;
    }
    
    fd_set read_fds;
    struct timeval tv;
    
    while (running_) {
        FD_ZERO(&read_fds);
        FD_SET(listen_fd_, &read_fds);
        tv.tv_sec = 0;
        tv.tv_usec = 100000;
        
        int ret = select(listen_fd_ + 1, &read_fds, nullptr, nullptr, &tv);
        
        if (ret < 0) {
            if (errno == EINTR) continue;
            continue;
        }
        
        if (ret == 0) continue;
        
        if (FD_ISSET(listen_fd_, &read_fds)) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int client_fd = accept(listen_fd_, (struct sockaddr*)&client_addr, &client_len);
            
            if (client_fd < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
                continue;
            }
            
            int flags = fcntl(client_fd, F_GETFL, 0);
            fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
            
            std::string client_ip = inet_ntoa(client_addr.sin_addr);
            uint16_t client_port = ntohs(client_addr.sin_port);
            core::SocketAddress peer_addr = core::SocketAddress::FromIpPort(client_ip, client_port);
            
            auto conn = std::make_shared<core::TCPConnection>(
                client_fd,
                peer_addr,
                io_handler_,
                buffer_manager_,
                scheduler_,
                dispatcher_
            );
            
            conn->init();
            conn->SetConnectionId(connection_manager_->GetConnectionCount() + 1);
            connection_manager_->RegisterConnection(conn);
            
            onNewConnection(conn);
        }
    }
}

// 新客户端连接建立时的初始化回调绑定
void HttpServer::onNewConnection(std::shared_ptr<core::IConnection> conn) {
    conn->SetDataCallback([this](std::shared_ptr<core::IConnection> c, std::string_view data) {
        onData(c, data);
    });
    conn->SetErrorCallback([this](std::shared_ptr<core::IConnection> c, std::error_code ec) {
        onError(c, ec);
    });
}

// 接收连接数据并转发给HTTP协议处理器
void HttpServer::onData(std::shared_ptr<core::IConnection> conn, std::string_view data) {
    if (protocol_handler_) {
        protocol_handler_->OnData(conn, data);
    }
}

// 连接发生错误时关闭连接
void HttpServer::onError(std::shared_ptr<core::IConnection> conn, std::error_code ec) {
    conn->Close();
}

// 启动服务器，开始监听指定地址和端口
void HttpServer::Listen(const std::string& host, uint16_t port) {
    host_ = host;
    port_ = port;
    
    if (!createListeningSocket(host, port)) {
        return;
    }

    running_ = true;
    accept_thread_ = std::make_unique<std::thread>([this]() { acceptLoop(); });

    std::cout << "\n🚀 HTTP server listening on " << host << ":" << port << std::endl;
}

// 停止服务器，关闭线程与套接字
void HttpServer::Stop() {
    running_ = false;
    if (accept_thread_ && accept_thread_->joinable()) {
        accept_thread_->join();
    }
    if (listen_fd_ >= 0) {
        close(listen_fd_);
        listen_fd_ = -1;
    }
}

} // namespace httpserver::application::http
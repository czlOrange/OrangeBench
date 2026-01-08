// src/interface/tcp_server.hpp
#pragma once

#include "httpserver/core/event_loop.hpp"
#include "httpserver/core/connection.hpp"
#include "httpserver/net/socket.hpp"
#include <memory>
#include <atomic>
#include <thread>

namespace httpserver::interface {

class TcpServer {
public:
    using ConnectionCallback = std::function<void(core::Connection::Ptr)>;
    using MessageCallback = std::function<void(core::Connection::Ptr, const std::string&)>;
    
    TcpServer(const std::string& ip, uint16_t port);
    ~TcpServer();
    
    // 服务器控制
    bool start();
    void stop();
    bool is_running() const { return running_; }
    
    // 回调设置
    void set_connection_callback(ConnectionCallback cb) { conn_callback_ = cb; }
    void set_message_callback(MessageCallback cb) { msg_callback_ = cb; }
    void set_close_callback(ConnectionCallback cb) { close_callback_ = cb; }
    
    // 配置
    void set_thread_num(int num) { thread_num_ = num; }
    void set_backlog(int backlog) { backlog_ = backlog; }
    void set_max_connections(size_t max) { max_connections_ = max; }
    
    // 统计
    size_t connection_count() const;
    size_t total_connections() const { return total_connections_; }
    size_t total_messages() const { return total_messages_; }
    
private:
    void accept_loop();  // 接受连接线程
    void worker_loop(int index);  // 工作线程
    void on_connection(core::Connection::Ptr conn);
    void on_message(core::Connection::Ptr conn, const std::string& msg);
    void on_close(core::Connection::Ptr conn);
    
private:
    net::NetAddress listen_addr_;
    std::unique_ptr<net::Socket> listen_socket_;
    
    std::atomic<bool> running_{false};
    std::thread accept_thread_;
    std::vector<std::thread> worker_threads_;
    std::vector<std::unique_ptr<core::EventLoop>> event_loops_;
    
    // 回调函数
    ConnectionCallback conn_callback_;
    MessageCallback msg_callback_;
    ConnectionCallback close_callback_;
    
    // 配置
    int thread_num_{1};  // 工作线程数（0表示单线程）
    int backlog_{1024};
    size_t max_connections_{10000};
    
    // 统计
    std::atomic<size_t> total_connections_{0};
    std::atomic<size_t> current_connections_{0};
    std::atomic<size_t> total_messages_{0};
};

} // namespace httpserver::interface
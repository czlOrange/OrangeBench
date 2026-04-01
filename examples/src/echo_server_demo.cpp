// examples/echo_server.cpp
#include "httpserver/interface/tcp_server.hpp"
#include <iostream>
#include <csignal>

std::atomic<bool> running{true};

void signal_handler(int sig) {
    std::cout << "\n收到信号 " << sig << "，正在关闭服务器..." << std::endl;
    running = false;
}

int main(int argc, char* argv[]) {
    // 设置信号处理
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    try {
        // 创建服务器
        httpserver::interface::TcpServer server("0.0.0.0", 8080);
        
        // 设置回调
        server.set_connection_callback([](auto conn) {
            std::cout << "新连接: " << conn->peer_address() << std::endl;
        });
        
        server.set_message_callback([](auto conn, const std::string& msg) {
            // 回显消息
            conn->send("Echo: " + msg);
            std::cout << "收到消息(" << conn->peer_address() << "): " 
                      << msg.substr(0, std::min(msg.size(), size_t(50))) 
                      << (msg.size() > 50 ? "..." : "") << std::endl;
        });
        
        server.set_close_callback([](auto conn) {
            std::cout << "连接关闭: " << conn->peer_address() << std::endl;
        });
        
        // 配置服务器
        server.set_thread_num(4);  // 4个工作线程
        server.set_backlog(1024);
        server.set_max_connections(1000);
        server.ServeStatic("/", "./public");  // 将根路径映射到 ./public 目录
        
        // 启动服务器
        if (server.start()) {
            std::cout << "回显服务器已启动，监听端口 8080" << std::endl;
            std::cout << "按 Ctrl+C 停止服务器" << std::endl;
            
            // 主循环
            while (running && server.is_running()) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                
                // 每10秒输出统计信息
                static int count = 0;
                if (++count % 10 == 0) {
                    std::cout << "活跃连接: " << server.connection_count() 
                              << ", 总连接: " << server.total_connections()
                              << ", 总消息: " << server.total_messages() << std::endl;
                }
            }
            
            // 停止服务器
            server.stop();
            std::cout << "服务器已停止" << std::endl;
            
        } else {
            std::cerr << "服务器启动失败" << std::endl;
            return 1;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "错误: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
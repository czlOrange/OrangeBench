// examples/simple_client.cpp
#include "httpserver/net/socket.hpp"
#include "httpserver/net/address.hpp"
#include <iostream>
#include <string>

int main() {
    try {
        // 创建Socket
        auto socket = httpserver::net::create_socket(httpserver::net::SocketType::TCP);
        
        // 连接服务器
        httpserver::net::NetAddress server_addr("127.0.0.1", 8080);
        if (!socket->connect(server_addr)) {
            std::cerr << "连接失败: " << socket->error_string() << std::endl;
            return 1;
        }
        
        std::cout << "已连接到服务器 " << server_addr.to_string() << std::endl;
        
        // 发送数据
        std::string message;
        while (std::getline(std::cin, message)) {
            if (message == "quit" || message == "exit") {
                break;
            }
            
            // 发送消息
            ssize_t sent = socket->send(message.data(), message.size());
            if (sent < 0) {
                std::cerr << "发送失败: " << socket->error_string() << std::endl;
                break;
            }
            
            std::cout << "已发送 " << sent << " 字节" << std::endl;
            
            // 接收响应
            char buffer[1024];
            ssize_t received = socket->recv(buffer, sizeof(buffer) - 1);
            if (received > 0) {
                buffer[received] = '\0';
                std::cout << "收到响应: " << buffer << std::endl;
            } else if (received == 0) {
                std::cout << "服务器关闭连接" << std::endl;
                break;
            } else {
                std::cerr << "接收失败: " << socket->error_string() << std::endl;
                break;
            }
        }
        
        // 关闭连接
        socket->close();
        std::cout << "连接已关闭" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "错误: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
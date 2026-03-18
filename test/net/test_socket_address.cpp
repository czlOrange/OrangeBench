#include <iostream>
#include <string>
#include <cassert>
#include <memory>
#include <arpa/inet.h>

// 包含头文件
#include "httpserver/net/address.hpp"
#include "httpserver/net/socket.hpp"

using namespace httpserver::net;

// 封装：测试 NetAddress 核心功能
bool test_net_address() {
    std::cout << "1. 测试 NetAddress..." << std::endl;
    try {
        // 测试从端口创建
        NetAddress addr1(8080);
        std::cout << "   创建端口8080的地址: " << addr1.to_string() << std::endl;
        assert(addr1.port() == 8080);
        assert(addr1.ip() == "0.0.0.0");
        
        // 测试带IP的构造函数
        NetAddress addr2("127.0.0.1", 9000);
        std::cout << "   创建127.0.0.1:9000的地址: " << addr2.to_string() << std::endl;
        assert(addr2.port() == 9000);
        assert(addr2.ip() == "127.0.0.1");
        
        // 测试 from_ip_port (重载版本1)
        NetAddress addr3 = NetAddress::from_ip_port("192.168.1.1", 8080);
        std::cout << "   解析192.168.1.1:8080: " << addr3.to_string() << std::endl;
        assert(addr3.ip() == "192.168.1.1");
        assert(addr3.port() == 8080);
        
        // 测试 from_ip_port (重载版本2)
        NetAddress addr4 = NetAddress::from_ip_port("10.0.0.1:1234");
        std::cout << "   解析10.0.0.1:1234: " << addr4.to_string() << std::endl;
        assert(addr4.ip() == "10.0.0.1");
        assert(addr4.port() == 1234);
        
        // 测试 family 方法
        std::cout << "   地址类型: " << 
            (addr2.family() == NetAddress::Family::IPv4 ? "IPv4" : 
             (addr2.family() == NetAddress::Family::IPv6 ? "IPv6" : "Unix")) << std::endl;
        
        std::cout << "   NetAddress 测试通过！\n" << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "   NetAddress 测试失败: " << e.what() << std::endl;
        return false;
    }
}

// 封装：测试 Socket 核心接口
bool test_socket_interface() {
    std::cout << "2. 测试 Socket 接口..." << std::endl;
    try {
        // 测试SocketType枚举
        std::cout << "   Socket类型枚举: ";
        std::cout << "TCP=" << (int)SocketType::TCP << ", ";
        std::cout << "UDP=" << (int)SocketType::UDP << std::endl;
        
        // 测试SocketOptions结构
        SocketOptions options;
        options.reuse_addr = true;
        options.tcp_no_delay = true;
        options.keep_alive = true;
        std::cout << "   Socket选项: reuse_addr=" << options.reuse_addr 
                  << ", tcp_no_delay=" << options.tcp_no_delay << std::endl;
        
        // 尝试创建Socket
        std::cout << "   创建TCP Socket..." << std::endl;
        auto tcp_socket = create_socket(SocketType::TCP);
        if (tcp_socket->is_valid()) {
            std::cout << "   TCP Socket创建成功 (fd=" << tcp_socket->fd() << ")" << std::endl;
        }
        
        std::cout << "   Socket 接口测试通过！\n" << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "   Socket 测试失败: " << e.what() << std::endl;
        return false;
    }
}

// 封装：测试网络地址转换
bool test_address_conversion() {
    std::cout << "3. 测试网络地址转换..." << std::endl;
    try {
        // 创建sockaddr_in结构
        sockaddr_in sa;
        sa.sin_family = AF_INET;
        sa.sin_port = htons(12345);
        inet_pton(AF_INET, "10.0.0.1", &sa.sin_addr);
        
        // 转换为NetAddress
        NetAddress addr(sa);
        std::cout << "   sockaddr_in转换: " << addr.to_string() << std::endl;
        assert(addr.ip() == "10.0.0.1");
        assert(addr.port() == 12345);
        
        // 测试sockaddr_ptr
        const sockaddr* sa_ptr = addr.sockaddr_ptr();
        socklen_t len = addr.sockaddr_len();
        std::cout << "   结构指针和长度: ptr=" << (void*)sa_ptr << ", len=" << len << std::endl;
        assert(sa_ptr != nullptr);
        assert(len == sizeof(sockaddr_in));
        
        std::cout << "   地址转换测试通过！\n" << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "   地址转换测试失败: " << e.what() << std::endl;
        return false;
    }
}

// 封装：测试异常处理逻辑
bool test_exception_handling() {
    std::cout << "4. 测试异常处理..." << std::endl;
    try {
        // 尝试解析无效地址
        auto addr = NetAddress::from_ip_port("invalid:format");
        std::cerr << "   错误：应该抛出异常！" << std::endl;
        return false;
    } catch (const std::exception& e) {
        std::cout << "   正确捕获异常: " << e.what() << std::endl;
        std::cout << "   异常处理测试通过！\n" << std::endl;
        return true;
    }
}

// 主函数：统一调用所有测试函数
int main() {
    std::cout << "=== 简单测试 NetAddress 和 Socket ===\n" << std::endl;

    // 按顺序执行测试，任意一个失败则整体退出
    if (!test_net_address()) {
        return 1;
    }
    if (!test_socket_interface()) {
        return 1;
    }
    if (!test_address_conversion()) {
        return 1;
    }
    if (!test_exception_handling()) {
        return 1;
    }
    
    std::cout << "\n=== 所有测试通过！ ===" << std::endl;
    std::cout << "NetAddress 和 Socket 接口正常工作" << std::endl;
    
    return 0;
}
// ============================================================================
// 文件: examples/ringbuffer_demo.cpp
// 描述: 环形缓冲区使用示例
// ============================================================================

#include "httpserver/net/buffer.hpp"
#include <iostream>
#include <thread>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#endif

using namespace httpserver::net;

void demo_basic_operations() {
    std::cout << "\n=== 基础操作示例 ===\n";
    
    RingBuffer buffer(16); // 小缓冲区便于观察
    
    // 写入数据
    std::string msg = "Hello";
    buffer.write(msg);
    std::cout << "写入: " << msg << std::endl;
    std::cout << "可读字节: " << buffer.readable_bytes() << std::endl;
    
    // 读取数据
    auto read_msg = buffer.read_string(5);
    std::cout << "读取: " << read_msg << std::endl;
    std::cout << "可读字节: " << buffer.readable_bytes() << std::endl;
}

void demo_zero_copy() {
    std::cout << "\n=== 零拷贝操作示例 ===\n";
    
    RingBuffer buffer(1024);
    
    // 准备数据
    std::string data = "This is a zero-copy example";
    buffer.write(data);
    
    // 获取可读区域
    auto [area1, area2] = buffer.readable_areas();
    
    std::cout << "可读区域1: " << area1.len << " 字节\n";
    if (area1.data) {
        std::cout << "  内容: " << std::string_view((const char*)area1.data, area1.len) << std::endl;
    }
    
    if (area2.data) {
        std::cout << "可读区域2: " << area2.len << " 字节\n";
        std::cout << "  内容: " << std::string_view((const char*)area2.data, area2.len) << std::endl;
    }
    
    // 标记已读取
    buffer.has_read(area1.len + (area2.len ? area2.len : 0));
    std::cout << "读取后可读字节: " << buffer.readable_bytes() << std::endl;
}

void demo_search() {
    std::cout << "\n=== 搜索功能示例 ===\n";
    
    RingBuffer buffer(32);
    
    // 写入HTTP请求
    buffer.write("GET /index.html HTTP/1.1\r\n");
    buffer.write("Host: localhost\r\n");
    buffer.write("\r\n");
    
    // 查找HTTP头结束标志
    size_t pos = buffer.find("\r\n\r\n");
    if (pos != std::string::npos) {
        std::cout << "找到HTTP头结束位置: " << pos << std::endl;
        
        // 读取HTTP头
        auto header = buffer.read_string(pos + 4);
        std::cout << "HTTP头:\n" << header << std::endl;
    }
    
    // 查找单个字符
    pos = buffer.find('/');
    if (pos != std::string::npos) {
        std::cout << "找到 '/' 在位置: " << pos << std::endl;
    }
}

void demo_network_integration() {
    std::cout << "\n=== 网络集成示例 ===\n";
    
    // 模拟网络接收
    RingBuffer recv_buffer(4096);
    
    // 模拟收到数据
    const char* packets[] = {
        "HTTP/1.1 200 OK\r\n",
        "Content-Type: text/html\r\n",
        "Content-Length: 13\r\n",
        "\r\n",
        "Hello, World!"
    };
    
    // 接收数据（零拷贝场景）
    for (auto* packet : packets) {
        size_t len = strlen(packet);
        recv_buffer.ensure_writable(len);
        
        // 获取可写区域
        auto [area1, area2] = recv_buffer.writable_areas();
        
        // 模拟从socket接收
        std::memcpy((void*)area1.data, packet, std::min(len, area1.len));
        if (len > area1.len && area2.data) {
            std::memcpy((void*)area2.data, packet + area1.len, len - area1.len);
        }
        
        recv_buffer.has_written(len);
        std::cout << "收到 " << len << " 字节，总缓冲: " 
                  << recv_buffer.readable_bytes() << std::endl;
    }
    
    // 处理收到的完整消息
    std::cout << "\n完整消息:\n";
    while (!recv_buffer.empty()) {
        auto [r1, r2] = recv_buffer.readable_areas();
        
        if (r1.data) {
            std::cout.write((const char*)r1.data, r1.len);
        }
        if (r2.data) {
            std::cout.write((const char*)r2.data, r2.len);
        }
        
        recv_buffer.has_read(r1.len + (r2.len ? r2.len : 0));
    }
    std::cout << std::endl;
}

void demo_auto_expand() {
    std::cout << "\n=== 自动扩容示例 ===\n";
    
    RingBuffer buffer(8); // 很小的初始容量
    std::cout << "初始容量: " << buffer.capacity() << std::endl;
    
    // 写入数据，触发自动扩容
    for (int i = 0; i < 5; ++i) {
        std::string data(4, 'A' + i); // "AAAA", "BBBB", ...
        buffer.write(data);
        std::cout << "写入 " << data << " 后容量: " 
                  << buffer.capacity() << std::endl;
    }
}

void demo_shrink() {
    std::cout << "\n=== 收缩示例 ===\n";
    
    RingBuffer buffer(1024);
    std::cout << "初始容量: " << buffer.capacity() << std::endl;
    
    // 写入大量数据
    std::string large_data(800, 'X');
    buffer.write(large_data);
    std::cout << "写入后容量: " << buffer.capacity() << std::endl;
    
    // 读取部分数据
    buffer.read_string(700);
    std::cout << "读取后可读: " << buffer.readable_bytes() << std::endl;
    
    // 收缩
    buffer.shrink_to_fit();
    std::cout << "收缩后容量: " << buffer.capacity() << std::endl;
}

int main() {
    std::cout << "=== 环形缓冲区演示 ===\n";
    
    demo_basic_operations();
    demo_zero_copy();
    demo_search();
    demo_network_integration();
    demo_auto_expand();
    demo_shrink();
    
    return 0;
}
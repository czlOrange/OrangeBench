// include/httpserver/net/address.hpp
#pragma once

#include <string>
#include <cstdint>
#include <netinet/in.h>

namespace httpserver::net {

/**
 * @brief 网络地址封装类
 * 
 * 支持IPv4/IPv6，提供统一的地址表示
 */
class NetAddress {
public:
    // 地址类型
    enum class Family {
        IPv4,
        IPv6,
        Unix  // Unix域套接字
    };
    
    // ========== 构造函数 ==========
    
    NetAddress();
    explicit NetAddress(uint16_t port);
    NetAddress(const std::string& ip, uint16_t port);
    explicit NetAddress(const sockaddr_in& addr);
    explicit NetAddress(const sockaddr_in6& addr);
    
    // ========== 获取地址信息 ==========
    
    Family family() const;
    std::string ip() const;
    uint16_t port() const;
    std::string to_string() const;
    
    // ========== 转换为系统结构 ==========
    
    const sockaddr* sockaddr_ptr() const;
    socklen_t sockaddr_len() const;
    
    // ========== 静态工厂方法 ==========
    
    static NetAddress from_ip_port(const std::string& ip, uint16_t port);
    static NetAddress from_ip_port(const std::string& ip_port);
    static NetAddress from_hostname(const std::string& hostname, uint16_t port);
    
    // ========== 比较操作符 ==========
    
    bool operator==(const NetAddress& other) const;
    bool operator!=(const NetAddress& other) const;
    
private:
    Family family_;
    union {
        sockaddr_in  addr_in_;
        sockaddr_in6 addr_in6_;
    };
};

} // namespace httpserver::net
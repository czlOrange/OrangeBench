// src/net/address.cpp
#include "httpserver/net/address.hpp"
#include <arpa/inet.h>
#include <cstring>
#include <stdexcept>
#include <sstream>

namespace httpserver::net {

// ========== 构造函数实现 ==========

NetAddress::NetAddress() : family_(Family::IPv4) {
    std::memset(&addr_in_, 0, sizeof(addr_in_));
    addr_in_.sin_family = AF_INET;
}

NetAddress::NetAddress(uint16_t port) : family_(Family::IPv4) {
    std::memset(&addr_in_, 0, sizeof(addr_in_));
    addr_in_.sin_family = AF_INET;
    addr_in_.sin_addr.s_addr = INADDR_ANY;
    addr_in_.sin_port = htons(port);
}

NetAddress::NetAddress(const std::string& ip, uint16_t port) : family_(Family::IPv4) {
    std::memset(&addr_in_, 0, sizeof(addr_in_));
    addr_in_.sin_family = AF_INET;
    addr_in_.sin_port = htons(port);
    
    if (inet_pton(AF_INET, ip.c_str(), &addr_in_.sin_addr) <= 0) {
        throw std::invalid_argument("Invalid IPv4 address: " + ip);
    }
}

NetAddress::NetAddress(const sockaddr_in& addr) : family_(Family::IPv4) {
    addr_in_ = addr;
}

NetAddress::NetAddress(const sockaddr_in6& addr) : family_(Family::IPv6) {
    addr_in6_ = addr;
}

// ========== 获取地址信息实现 ==========

NetAddress::Family NetAddress::family() const { 
    return family_; 
}

std::string NetAddress::ip() const {
    char buffer[INET6_ADDRSTRLEN] = {0};
    
    if (family_ == Family::IPv4) {
        inet_ntop(AF_INET, &addr_in_.sin_addr, buffer, sizeof(buffer));
    } else if (family_ == Family::IPv6) {
        inet_ntop(AF_INET6, &addr_in6_.sin6_addr, buffer, sizeof(buffer));
    } else {
        return "unix";
    }
    
    return std::string(buffer);
}

uint16_t NetAddress::port() const {
    if (family_ == Family::IPv4) {
        return ntohs(addr_in_.sin_port);
    } else if (family_ == Family::IPv6) {
        return ntohs(addr_in6_.sin6_port);
    }
    return 0;
}

std::string NetAddress::to_string() const {
    if (family_ == Family::IPv4) {
        return ip() + ":" + std::to_string(port());
    } else if (family_ == Family::IPv6) {
        return "[" + ip() + "]:" + std::to_string(port());
    } else {
        return "unix";
    }
}

// ========== 转换为系统结构实现 ==========

const sockaddr* NetAddress::sockaddr_ptr() const {
    if (family_ == Family::IPv4) {
        return reinterpret_cast<const sockaddr*>(&addr_in_);
    } else if (family_ == Family::IPv6) {
        return reinterpret_cast<const sockaddr*>(&addr_in6_);
    }
    return nullptr;
}

socklen_t NetAddress::sockaddr_len() const {
    if (family_ == Family::IPv4) {
        return sizeof(addr_in_);
    } else if (family_ == Family::IPv6) {
        return sizeof(addr_in6_);
    }
    return 0;
}

// ========== 静态工厂方法实现 ==========

NetAddress NetAddress::from_ip_port(const std::string& ip, uint16_t port) {
    return NetAddress(ip, port);
}

NetAddress NetAddress::from_ip_port(const std::string& ip_port) {
    // 解析格式: "192.168.1.1:8080" 或 "[2001:db8::1]:8080"
    size_t colon_pos = ip_port.find_last_of(':');
    if (colon_pos == std::string::npos) {
        throw std::invalid_argument("Invalid ip:port format: " + ip_port);
    }
    
    std::string ip_str = ip_port.substr(0, colon_pos);
    std::string port_str = ip_port.substr(colon_pos + 1);
    
    // 去除IPv6地址的方括号
    if (ip_str.front() == '[' && ip_str.back() == ']') {
        ip_str = ip_str.substr(1, ip_str.length() - 2);
    }
    
    uint16_t port = static_cast<uint16_t>(std::stoi(port_str));
    
    // 尝试IPv4
    sockaddr_in addr4;
    std::memset(&addr4, 0, sizeof(addr4));
    if (inet_pton(AF_INET, ip_str.c_str(), &addr4.sin_addr) == 1) {
        addr4.sin_family = AF_INET;
        addr4.sin_port = htons(port);
        return NetAddress(addr4);
    }
    
    // 尝试IPv6
    sockaddr_in6 addr6;
    std::memset(&addr6, 0, sizeof(addr6));
    if (inet_pton(AF_INET6, ip_str.c_str(), &addr6.sin6_addr) == 1) {
        addr6.sin6_family = AF_INET6;
        addr6.sin6_port = htons(port);
        return NetAddress(addr6);
    }
    
    throw std::invalid_argument("Invalid IP address format: " + ip_str);
}

NetAddress NetAddress::from_hostname(const std::string& hostname, uint16_t port) {
    // 简化实现，只支持IP地址
    sockaddr_in addr4;
    std::memset(&addr4, 0, sizeof(addr4));
    if (inet_pton(AF_INET, hostname.c_str(), &addr4.sin_addr) == 1) {
        addr4.sin_family = AF_INET;
        addr4.sin_port = htons(port);
        return NetAddress(addr4);
    }
    
    sockaddr_in6 addr6;
    std::memset(&addr6, 0, sizeof(addr6));
    if (inet_pton(AF_INET6, hostname.c_str(), &addr6.sin6_addr) == 1) {
        addr6.sin6_family = AF_INET6;
        addr6.sin6_port = htons(port);
        return NetAddress(addr6);
    }
    
    throw std::invalid_argument("Invalid hostname or IP address: " + hostname);
}

// ========== 比较操作符实现 ==========

bool NetAddress::operator==(const NetAddress& other) const {
    if (family_ != other.family_) return false;
    
    if (family_ == Family::IPv4) {
        return addr_in_.sin_addr.s_addr == other.addr_in_.sin_addr.s_addr &&
               addr_in_.sin_port == other.addr_in_.sin_port;
    } else if (family_ == Family::IPv6) {
        return memcmp(&addr_in6_.sin6_addr, &other.addr_in6_.sin6_addr, 
                     sizeof(addr_in6_.sin6_addr)) == 0 &&
               addr_in6_.sin6_port == other.addr_in6_.sin6_port;
    }
    return false;
}

bool NetAddress::operator!=(const NetAddress& other) const {
    return !(*this == other);
}

} // namespace httpserver::net
//我在做测试
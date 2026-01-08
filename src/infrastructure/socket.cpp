// src/infrastructure/socket.cpp - Linux实现
#include "httpserver/infrastructure/isocket.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdexcept>

namespace httpserver::infrastructure {

class LinuxSocket : public ISocket {
private:
    int sock_fd_{-1};
    SocketType type_;
    SocketAddress local_addr_;
    SocketAddress remote_addr_;
    
public:
    explicit LinuxSocket(SocketType type) : type_(type) {
        int domain = AF_INET;
        int socket_type = (type == SocketType::TCP) ? SOCK_STREAM : SOCK_DGRAM;
        int protocol = 0;
        
        sock_fd_ = ::socket(domain, socket_type, protocol);
        if (sock_fd_ < 0) {
            throw std::runtime_error("Failed to create socket");
        }
    }
    
    LinuxSocket(int fd, SocketType type, const SocketAddress& remote) 
        : sock_fd_(fd), type_(type), remote_addr_(remote) {
    }
    
    ~LinuxSocket() override {
        if (sock_fd_ >= 0) {
            ::close(sock_fd_);
        }
    }
    
    // 实现接口...
    
};
}
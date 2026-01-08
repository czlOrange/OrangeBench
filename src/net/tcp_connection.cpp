// src/core/tcp_connection.cpp
#include "httpserver/core/connection.hpp"
#include "httpserver/net/buffer.hpp"

namespace httpserver::core {

class TcpConnection : public Connection {
public:
    TcpConnection(std::unique_ptr<net::Socket> socket) 
        : Connection(std::move(socket)),
          input_buffer_(4096),
          output_buffer_(4096) {
    }
    
    // 实现抽象方法
    net::RingBuffer& input_buffer() override { return input_buffer_; }
    net::RingBuffer& output_buffer() override { return output_buffer_; }
    
protected:
    void on_readable() override {
        // 读取数据到input_buffer_
        char buf[4096];
        while (true) {
            auto [iov1, iov2] = input_buffer_.writable_areas();
            if (iov1.len == 0 && iov2.len == 0) {
                input_buffer_.ensure_writable(4096);
                continue;
            }
            
            ssize_t n = socket_->recv(iov1.data, iov1.len);
            if (n > 0) {
                input_buffer_.has_written(n);
                bytes_received_ += n;
                
                if (event_callback_) {
                    event_callback_(ConnectionEvent::DATA_RECEIVED, 
                                   shared_from_this(), "");
                }
            } else if (n == 0) {
                // 对端关闭
                disconnect();
            } else {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;  // 没有更多数据
                }
                on_error(errno);
                break;
            }
        }
    }
    
    void on_writable() override {
        // 从output_buffer_发送数据
        while (output_buffer_.readable_bytes() > 0) {
            auto [iov1, iov2] = output_buffer_.readable_areas();
            
            if (iov1.len > 0) {
                ssize_t n = socket_->send(iov1.data, iov1.len);
                if (n > 0) {
                    output_buffer_.has_read(n);
                    bytes_sent_ += n;
                    
                    if (event_callback_) {
                        event_callback_(ConnectionEvent::DATA_SENT, 
                                       shared_from_this(), "");
                    }
                } else if (n < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        break;  // 缓冲区满
                    }
                    on_error(errno);
                    break;
                }
            }
            
            if (iov2.len > 0) {
                // 发送第二块数据
                // ...
            }
        }
    }
    
    void on_error(int error) override {
        state_ = ConnectionState::DISCONNECTED;
        if (event_callback_) {
            event_callback_(ConnectionEvent::ERROR_OCCURRED, 
                           shared_from_this(), strerror(error));
        }
        socket_->close();
    }
    
private:
    net::RingBuffer input_buffer_;
    net::RingBuffer output_buffer_;
};

} // namespace httpserver::core
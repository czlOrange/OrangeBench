// include/httpserver/net/buffer.hpp
#pragma once

#include <vector>
#include <cstddef>
#include <string>
#include <algorithm>

namespace httpserver::net {

/**
 * @brief 高效的环形缓冲区
 * 
 * 支持零拷贝读写，自动扩容，线程安全（外部同步）
 */
class RingBuffer {
public:
    explicit RingBuffer(size_t initial_capacity = 1024);
    
    // 写入数据
    size_t write(const void* data, size_t len);
    size_t write(const std::string& str);
    
    // 读取数据
    size_t read(void* buf, size_t len);
    std::string read_string(size_t len);
    
    // 零拷贝操作
    struct Iovec {
        const void* data;
        size_t len;
    };
    
    // 获取可写的连续内存区域（用于零拷贝写）
    std::pair<Iovec, Iovec> writable_areas() const;
    
    // 获取可读的连续内存区域（用于零拷贝读）
    std::pair<Iovec, Iovec> readable_areas() const;
    
    // 移动读写指针（用于零拷贝操作后）
    void has_written(size_t len);
    void has_read(size_t len);
    
    // 搜索
    size_t find(const std::string& pattern) const;
    size_t find(char c) const;
    
    // 状态查询
    size_t readable_bytes() const { 
        if (write_pos_ >= read_pos_) {
            return write_pos_ - read_pos_;
        } else {
            return capacity() - (read_pos_ - write_pos_);
        }
    }
    size_t writable_bytes() const { 
        size_t total = capacity() - readable_bytes();
        return total > 0 ? total - 1 : 0;
    }
    size_t capacity() const { return buffer_.size(); }
    bool empty() const { return readable_bytes() == 0; }
    bool full() const { return writable_bytes() == 0; }
    
    // 内存管理
    void ensure_writable(size_t len);
    void shrink_to_fit();      
    
private:
    void expand_if_needed(size_t len);
    size_t front_capacity() const;
    size_t back_capacity() const;
    
private:
    std::vector<char> buffer_;
    size_t read_pos_{0};
    size_t write_pos_{0};
};

} // namespace httpserver::net
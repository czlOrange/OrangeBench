#include "httpserver/core/tcp_connections/Buffer/memory_pool.hpp"
#include <iostream>

int main() {
    // 创建内存池
    auto pool = httpserver::core::IMemoryPool::CreateDefault();
    
    // 配置
    pool->SetMaxPoolSize(1024 * 1024 * 50); // 50MB
    pool->SetPreAllocation(true); // 启用预分配
    
    // 分配内存
    void* p1 = pool->AllocateBytes(100);
    void* p2 = pool->AllocateBytesAligned(256, 16);
    
    // 查看统计
    auto stats = pool->GetStats();
    std::cout << "已分配: " << stats.allocated_bytes << " 字节\n";
    std::cout << "使用中: " << stats.used_bytes << " 字节\n";
    
    // 释放内存
    pool->DeallocateBytes(p1);
    pool->DeallocateBytes(p2);
    
    // 清理
    pool->ClearPool();
    
    return 0;
}
// ============================================================================
// 文件: tests/ringbuffer_test.cpp
// 描述: RingBuffer 单元测试
// ============================================================================

#include <gtest/gtest.h>
#include "../../../include/httpserver/net/buffer.hpp"
#include <string>
#include <thread>
#include <vector>
#include <cstring>

using namespace httpserver::net;

// ============================================================================
// 基础操作测试
// ============================================================================

TEST(RingBufferTest, BasicWriteRead) {
    RingBuffer buffer(16);
    
    // 初始状态
    EXPECT_EQ(buffer.readable_bytes(), 0);
    EXPECT_EQ(buffer.writable_bytes(), 15); // 保留一个字节区分空/满
    EXPECT_TRUE(buffer.empty());
    EXPECT_FALSE(buffer.full());
    
    // 写入数据
    std::string msg = "Hello";
    size_t written = buffer.write(msg);
    EXPECT_EQ(written, 5);
    EXPECT_EQ(buffer.readable_bytes(), 5);
    EXPECT_EQ(buffer.writable_bytes(), 10); // 15-5=10
    EXPECT_FALSE(buffer.empty());
    EXPECT_FALSE(buffer.full());
    
    // 读取数据
    std::string read_msg = buffer.read_string(5);
    EXPECT_EQ(read_msg, "Hello");
    EXPECT_EQ(buffer.readable_bytes(), 0);
    EXPECT_EQ(buffer.writable_bytes(), 15);
    EXPECT_TRUE(buffer.empty());
}

TEST(RingBufferTest, WriteReadMultiple) {
    RingBuffer buffer(16);
    
    buffer.write("Hello");
    buffer.write(" ");
    buffer.write("World");
    
    EXPECT_EQ(buffer.readable_bytes(), 11);
    
    std::string result = buffer.read_string(11);
    EXPECT_EQ(result, "Hello World");
    EXPECT_TRUE(buffer.empty());
}

TEST(RingBufferTest, PartialRead) {
    RingBuffer buffer(16);
    
    buffer.write("Hello World");
    EXPECT_EQ(buffer.readable_bytes(), 11);
    
    std::string part1 = buffer.read_string(5);
    EXPECT_EQ(part1, "Hello");
    EXPECT_EQ(buffer.readable_bytes(), 6);
    
    std::string part2 = buffer.read_string(6);
    EXPECT_EQ(part2, " World");
    EXPECT_TRUE(buffer.empty());
}

// ============================================================================
// 环形特性测试
// ============================================================================

TEST(RingBufferTest, RingWrapping) {
    RingBuffer buffer(8);
    
    buffer.write("123456");
    EXPECT_EQ(buffer.readable_bytes(), 6);
    
    buffer.read_string(3);
    EXPECT_EQ(buffer.readable_bytes(), 3);
    
    buffer.write("789A");
    EXPECT_EQ(buffer.readable_bytes(), 7);
    
    std::string result = buffer.read_string(7);
    EXPECT_EQ(result, "456789A");
}

TEST(RingBufferTest, ExactFullCapacity) {
    RingBuffer buffer(8);
    
    buffer.write("1234567");
    EXPECT_TRUE(buffer.full());
    EXPECT_EQ(buffer.writable_bytes(), 0);
    
    buffer.read_string(1);
    EXPECT_FALSE(buffer.full());
    EXPECT_EQ(buffer.writable_bytes(), 1);
}

// ============================================================================
// 零拷贝操作测试
// ============================================================================

TEST(RingBufferTest, ZeroCopyReadableAreas) {
    RingBuffer buffer(16);
    
    buffer.write("Hello");
    
    auto [area1, area2] = buffer.readable_areas();
    
    EXPECT_NE(area1.data, nullptr);
    EXPECT_EQ(area1.len, 5);
    EXPECT_EQ(area2.data, nullptr);
    EXPECT_EQ(area2.len, 0);
    
    std::string_view sv1((const char*)area1.data, area1.len);
    EXPECT_EQ(sv1, "Hello");
    
    buffer.has_read(area1.len);
    EXPECT_TRUE(buffer.empty());
}

TEST(RingBufferTest, ZeroCopyWritableAreas) {
    RingBuffer buffer(16);
    
    auto [area1, area2] = buffer.writable_areas();
    
    EXPECT_NE(area1.data, nullptr);
    EXPECT_EQ(area1.len, 15);
    EXPECT_EQ(area2.data, nullptr);
    EXPECT_EQ(area2.len, 0);
    
    const char* data = "Hello";
    memcpy((void*)area1.data, data, 5);
    buffer.has_written(5);
    
    EXPECT_EQ(buffer.readable_bytes(), 5);
    EXPECT_EQ(buffer.read_string(5), "Hello");
}

TEST(RingBufferTest, ZeroCopyWrapping) {
    RingBuffer buffer(8);
    
    buffer.write("12345");
    buffer.read_string(3);
    
    auto [w1, w2] = buffer.writable_areas();
    
    // 验证至少有一段可写区域
    EXPECT_TRUE((w1.len > 0) || (w2.len > 0));
    EXPECT_NE(w1.data, nullptr);
    
    if (w2.len > 0) {
        EXPECT_NE(w2.data, nullptr);
    }
}

// ============================================================================
// 搜索功能测试
// ============================================================================

TEST(RingBufferTest, FindString) {
    RingBuffer buffer(32);
    
    buffer.write("Hello World");
    
    size_t pos = buffer.find("World");
    EXPECT_EQ(pos, 6);
    
    pos = buffer.find("Hello");
    EXPECT_EQ(pos, 0);
    
    pos = buffer.find("NotExist");
    EXPECT_EQ(pos, std::string::npos);
}

TEST(RingBufferTest, FindChar) {
    RingBuffer buffer(32);
    
    buffer.write("Hello World");
    
    size_t pos = buffer.find('W');
    EXPECT_EQ(pos, 6);
    
    pos = buffer.find('H');
    EXPECT_EQ(pos, 0);
    
    pos = buffer.find('x');
    EXPECT_EQ(pos, std::string::npos);
}

TEST(RingBufferTest, FindWrapping) {
    RingBuffer buffer(8);
    
    buffer.write("12345");
    buffer.read_string(3);
    buffer.write("678");
    
    size_t pos = buffer.find("567");
    EXPECT_EQ(pos, 1);
    
    pos = buffer.find("678");
    EXPECT_EQ(pos, 2);
    
    pos = buffer.find("89");
    EXPECT_EQ(pos, std::string::npos);
}

// ============================================================================
// 扩容测试
// ============================================================================

TEST(RingBufferTest, AutoExpand) {
    RingBuffer buffer(8);
    size_t initial_capacity = buffer.capacity();
    EXPECT_EQ(initial_capacity, 8);
    
    std::string data(20, 'A');
    buffer.write(data);
    
    EXPECT_GT(buffer.capacity(), initial_capacity);
    EXPECT_EQ(buffer.readable_bytes(), 20);
    
    std::string result = buffer.read_string(20);
    EXPECT_EQ(result, data);
}

TEST(RingBufferTest, EnsureWritable) {
    RingBuffer buffer(8);
    
    buffer.ensure_writable(20);
    EXPECT_GE(buffer.capacity(), 20);
    
    buffer.ensure_writable(50);
    EXPECT_GE(buffer.capacity(), 50);
}

// ============================================================================
// 收缩测试
// ============================================================================

TEST(RingBufferTest, ShrinkToFit) {
    RingBuffer buffer(1024);
    
    std::string data(800, 'X');
    buffer.write(data);
    EXPECT_EQ(buffer.capacity(), 1024);
    
    buffer.read_string(700);
    EXPECT_EQ(buffer.readable_bytes(), 100);
    
    buffer.shrink_to_fit();
    
    // 收缩后容量应该小于等于原容量
    EXPECT_LE(buffer.capacity(), 1024);
    EXPECT_EQ(buffer.readable_bytes(), 100);
    
    std::string remaining = buffer.read_string(100);
    EXPECT_EQ(remaining, std::string(100, 'X'));
}

TEST(RingBufferTest, ShrinkEmpty) {
    RingBuffer buffer(1024);
    
    buffer.write("test");
    buffer.read_string(4);
    
    size_t old_capacity = buffer.capacity();
    buffer.shrink_to_fit();
    
    // 空缓冲区收缩后容量应该小于等于原容量
    EXPECT_LE(buffer.capacity(), old_capacity);
    EXPECT_TRUE(buffer.empty());
}

// ============================================================================
// 边界条件测试
// ============================================================================

TEST(RingBufferTest, ZeroLengthOperations) {
    RingBuffer buffer(16);
    
    size_t written = buffer.write("");
    EXPECT_EQ(written, 0);
    EXPECT_TRUE(buffer.empty());
    
    std::string empty = buffer.read_string(0);
    EXPECT_TRUE(empty.empty());
    
    empty = buffer.read_string(5);
    EXPECT_TRUE(empty.empty());
}

TEST(RingBufferTest, LargeData) {
    RingBuffer buffer(1024);
    
    std::string large_data(1024 * 1024, 'A');
    buffer.write(large_data);
    
    EXPECT_EQ(buffer.readable_bytes(), large_data.size());
    
    const size_t chunk_size = 16384;
    size_t total_read = 0;
    
    while (total_read < large_data.size()) {
        size_t to_read = std::min(chunk_size, large_data.size() - total_read);
        std::string chunk = buffer.read_string(to_read);
        EXPECT_EQ(chunk.size(), to_read);
        EXPECT_EQ(chunk, std::string(to_read, 'A'));
        total_read += to_read;
    }
    
    EXPECT_TRUE(buffer.empty());
}

// ============================================================================
// 主函数
// ============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
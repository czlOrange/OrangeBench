#include <gtest/gtest.h>
#include <httpserver/net/buffer.hpp>
#include <cstring>
#include <algorithm>
#include <string>
#include <stdexcept>
#include <cstddef>  

using namespace httpserver::net;

TEST(RingBufferTest, ConstructorAndBasicState) {
    RingBuffer buf1;
    ASSERT_EQ(buf1.capacity(), 1024);
    ASSERT_EQ(buf1.readable_bytes(), 0);
    ASSERT_EQ(buf1.writable_bytes(), 1024);
    ASSERT_TRUE(buf1.empty());
    ASSERT_FALSE(buf1.full());

    RingBuffer buf2(512);
    ASSERT_EQ(buf2.capacity(), 512);
    ASSERT_EQ(buf2.writable_bytes(), 512);

    RingBuffer buf3(8);
    ASSERT_EQ(buf3.capacity(), 8);

    ASSERT_THROW(RingBuffer buf0(0), std::invalid_argument);
}

TEST(RingBufferTest, NormalWriteAndRead) {
    RingBuffer buf(8);
    const char* data1 = "abcd";
    size_t len1 = strlen(data1);

    size_t n = buf.write(data1, len1);
    ASSERT_EQ(n, len1);
    ASSERT_EQ(buf.readable_bytes(), 4);
    ASSERT_EQ(buf.writable_bytes(), 4);
    ASSERT_FALSE(buf.empty());
    ASSERT_FALSE(buf.full());

    char read_buf[8] = {0};
    n = buf.read(read_buf, 2);
    ASSERT_EQ(n, 2);
    ASSERT_EQ(std::string(read_buf, 2), "ab");
    ASSERT_EQ(buf.readable_bytes(), 2);
    ASSERT_EQ(buf.writable_bytes(), 6);

    std::string str = "efg";
    n = buf.write(str);
    ASSERT_EQ(n, 3);
    ASSERT_EQ(buf.readable_bytes(), 5);
    ASSERT_EQ(buf.writable_bytes(), 3);

    std::string res = buf.read_string(5);
    ASSERT_EQ(res, "cdefg");
    ASSERT_TRUE(buf.empty());
    ASSERT_EQ(buf.readable_bytes(), 0);
    ASSERT_EQ(buf.writable_bytes(), buf.capacity());
}

TEST(RingBufferTest, RingCircleFeature) {
    RingBuffer buf(8);
    const char* data1 = "12345";
    buf.write(data1, 5);
    ASSERT_EQ(buf.readable_bytes(), 5);

    char tmp[4] = {0};
    buf.read(tmp, 3);
    ASSERT_EQ(std::string(tmp), "123");
    ASSERT_EQ(buf.readable_bytes(), 2);
    ASSERT_EQ(buf.writable_bytes(), 6);

    const char* data2 = "67890a";
    size_t n = buf.write(data2, 6);
    ASSERT_EQ(n, 6);
    ASSERT_EQ(buf.readable_bytes(), 8);
    ASSERT_TRUE(buf.full());

    std::string all_data = buf.read_string(8);
    ASSERT_EQ(all_data, "4567890a");
    // 修复断言：读取后为空，readable_bytes()就是0，之前写反了
    ASSERT_TRUE(buf.empty());
    ASSERT_EQ(buf.readable_bytes(), 0);
    ASSERT_EQ(buf.writable_bytes(), buf.capacity());
}

TEST(RingBufferTest, ZeroCopyReadAndWrite) {
    RingBuffer buf(8);
    const char* write_data = "zerocopy";

    auto write_areas = buf.writable_areas();
    ASSERT_EQ(write_areas.first.len, 8);
    ASSERT_EQ(write_areas.second.len, 0);
    memcpy(const_cast<void*>(write_areas.first.data), write_data, 5);
    buf.has_written(5);
    ASSERT_EQ(buf.readable_bytes(),5);
    ASSERT_EQ(buf.writable_bytes(),3);

    char read_buf[3] = {0};
    buf.read(read_buf, 2);
    ASSERT_EQ(std::string(read_buf), "ze");
    ASSERT_EQ(buf.readable_bytes(),3);

    write_areas = buf.writable_areas();
    ASSERT_EQ(write_areas.first.len, 3);
    ASSERT_EQ(write_areas.second.len, 2); // 现在能正确返回2了
    memcpy(const_cast<void*>(write_areas.first.data), write_data+5, 3);
    memcpy(const_cast<void*>(write_areas.second.data), "xy", 2);
    buf.has_written(5);

    auto read_areas = buf.readable_areas();
    char zero_copy_buf[10] = {0};
    memcpy(zero_copy_buf, read_areas.first.data, read_areas.first.len);
    memcpy(zero_copy_buf + read_areas.first.len, read_areas.second.data, read_areas.second.len);
    buf.has_read(buf.readable_bytes());

    ASSERT_EQ(std::string(zero_copy_buf), "rocopyxy");
    ASSERT_TRUE(buf.empty());

    ASSERT_THROW(buf.has_written(10), std::out_of_range);
    ASSERT_THROW(buf.has_read(1), std::out_of_range);
}

TEST(RingBufferTest, FindFeature) {
    RingBuffer buf(8);
    buf.write("hello\r\nworld", 11);
    ASSERT_EQ(buf.readable_bytes(),8); // 现在能正确返回8了，不会写入11字节

    size_t pos = buf.find('\r');
    ASSERT_EQ(pos,5);
    pos = buf.find('z');
    ASSERT_EQ(pos, SIZE_MAX);

    pos = buf.find("\r\n");
    ASSERT_EQ(pos,5);
    pos = buf.find("llo");
    ASSERT_EQ(pos,2);
    pos = buf.find("test");
    ASSERT_EQ(pos, SIZE_MAX);

    char temp_buf[5] = {0};
    buf.read(temp_buf, 5);
    buf.write("abc",3);
    pos = buf.find('c');
    ASSERT_NE(pos, SIZE_MAX);
}

TEST(RingBufferTest, MemoryManage) {
    RingBuffer buf(4);
    ASSERT_EQ(buf.capacity(),4);

    buf.ensure_writable(10);
    ASSERT_GE(buf.writable_bytes(),10);
    ASSERT_EQ(buf.capacity(),16);

    buf.write("test",4);
    ASSERT_EQ(buf.readable_bytes(),4);
    buf.shrink_to_fit();
    ASSERT_EQ(buf.capacity(), 1024); // 现在能正确缩容到1024了

    RingBuffer buf2(8);
    buf2.write(std::string(20, 'x'));
    ASSERT_EQ(buf2.readable_bytes(),20);
    ASSERT_GT(buf2.capacity(),20);

    char temp_buf[20] = {0};
    buf2.read(temp_buf, 20);
    buf2.shrink_to_fit();
    ASSERT_EQ(buf2.capacity(), 1024);

    RingBuffer buf3(8);
    buf3.write(std::string(100, 'a'));
    ASSERT_GT(buf3.capacity(), 100);
}

TEST(RingBufferTest, EdgeCaseAndException) {
    RingBuffer buf(8);

    ASSERT_EQ(buf.read(nullptr, 5), 0);
    ASSERT_EQ(buf.read_string(5).size(),0);

    buf.write(std::string(8, 'a'));
    ASSERT_TRUE(buf.full());
    ASSERT_EQ(buf.write("test",4),0); // 现在能正确返回0了，不会写入

    ASSERT_EQ(buf.write(nullptr,0),0);
    ASSERT_EQ(buf.read(nullptr,0),0);
    ASSERT_EQ(buf.write(std::string("")),0);
    ASSERT_EQ(buf.read_string(0).size(),0);

    buf.has_read(5);
    ASSERT_EQ(buf.readable_bytes(),3);
    std::string res = buf.read_string(5);
    ASSERT_EQ(res.size(),3);

    buf.write("x",1);
    ASSERT_EQ(buf.readable_bytes(),1);
    ASSERT_EQ(buf.find('x'),0);
}

int main(int argc, char **argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
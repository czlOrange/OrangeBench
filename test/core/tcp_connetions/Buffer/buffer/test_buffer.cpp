// test/core/buffer_test.cpp
#include "../../../../../include/httpserver/core/tcp_connections/Buffer/buffer.hpp"

#include <gtest/gtest.h>
#include <cstring>
#include <memory>

using namespace httpserver::core;

class BufferTest : public ::testing::Test {
protected:
    static void* alloc_test_memory(size_t size) {
        return ::operator new(size);
    }
    static void test_deleter(void* p) {
        ::operator delete(p);
    }
};

TEST_F(BufferTest, ConstructionAndDestruction) {
    void* mem = alloc_test_memory(1024);
    bool deleter_called = false;
    auto deleter = [&deleter_called](void* p) {
        deleter_called = true;
        ::operator delete(p);
    };
    {
        Buffer buf(mem, 1024, deleter);
        EXPECT_EQ(buf.data(), mem);
        EXPECT_EQ(buf.size(), 0);
        EXPECT_EQ(buf.capacity(), 1024);
    }
    EXPECT_TRUE(deleter_called);
}

TEST_F(BufferTest, MoveConstruction) {
    void* mem = alloc_test_memory(512);
    bool deleter_called = false;
    auto deleter = [&deleter_called](void* p) {
        deleter_called = true;
        ::operator delete(p);
    };
    Buffer buf1(mem, 512, deleter);
    buf1.resize(100);
    EXPECT_EQ(buf1.size(), 100);
    void* mem1 = buf1.data();
    Buffer buf2(std::move(buf1));
    EXPECT_EQ(buf1.data(), nullptr);
    EXPECT_EQ(buf1.size(), 0);
    EXPECT_EQ(buf1.capacity(), 0);
    EXPECT_EQ(buf2.data(), mem1);
    EXPECT_EQ(buf2.size(), 100);
    EXPECT_EQ(buf2.capacity(), 512);
}

TEST_F(BufferTest, MoveAssignment) {
    void* mem1 = alloc_test_memory(256);
    void* mem2 = alloc_test_memory(512);
    bool deleter1_called = false, deleter2_called = false;
    auto deleter1 = [&deleter1_called](void* p) { deleter1_called = true; ::operator delete(p); };
    auto deleter2 = [&deleter2_called](void* p) { deleter2_called = true; ::operator delete(p); };
    Buffer buf1(mem1, 256, deleter1);
    Buffer buf2(mem2, 512, deleter2);
    buf1.resize(50);
    buf2.resize(100);
    buf2 = std::move(buf1);
    EXPECT_EQ(buf1.data(), nullptr);
    EXPECT_EQ(buf1.size(), 0);
    EXPECT_EQ(buf1.capacity(), 0);
    EXPECT_EQ(buf2.data(), mem1);
    EXPECT_EQ(buf2.size(), 50);
    EXPECT_EQ(buf2.capacity(), 256);
    EXPECT_TRUE(deleter2_called);
}

TEST_F(BufferTest, Resize) {
    void* mem = alloc_test_memory(100);
    auto deleter = [](void* p) { ::operator delete(p); };
    Buffer buf(mem, 100, deleter);
    EXPECT_EQ(buf.size(), 0);
    buf.resize(50);
    EXPECT_EQ(buf.size(), 50);
    buf.resize(100);
    EXPECT_EQ(buf.size(), 100);
    EXPECT_THROW(buf.resize(101), std::out_of_range);
}

TEST_F(BufferTest, DataAccess) {
    void* mem = alloc_test_memory(100);
    auto deleter = [](void* p) { ::operator delete(p); };
    Buffer buf(mem, 100, deleter);
    char* data = buf.data();
    std::strcpy(data, "Hello");
    buf.resize(5);
    EXPECT_EQ(buf.size(), 5);
    EXPECT_STREQ(buf.data(), "Hello");
    const char* cdata = buf.data();
    EXPECT_STREQ(cdata, "Hello");
}

TEST_F(BufferTest, SelfMoveAssignment) {
    void* mem = alloc_test_memory(200);
    bool deleter_called = false;
    auto deleter = [&deleter_called](void* p) { deleter_called = true; ::operator delete(p); };
    Buffer buf(mem, 200, deleter);
    buf.resize(123);
    Buffer& ref = buf;
    buf = std::move(ref);
    EXPECT_EQ(buf.data(), mem);
    EXPECT_EQ(buf.size(), 123);
    EXPECT_EQ(buf.capacity(), 200);
    EXPECT_FALSE(deleter_called);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
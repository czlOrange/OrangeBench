// test/core/buffer_operator_test.cpp
#include "../../../../../include/httpserver/core/tcp_connections/Buffer/buffer_operator.hpp"
#include "../../../../../include/httpserver/core/tcp_connections/Buffer/buffer_factory.hpp"
#include "../../../../../include/httpserver/core/tcp_connections/Buffer/buffer_view.hpp"

#include <gtest/gtest.h>
#include <vector>
#include <cstring>

using namespace httpserver::core;

class BufferOperatorTest : public ::testing::Test {
protected:
    void SetUp() override {
        factory_ = IBufferFactory::CreateDefault();
        op_ = IBufferOperator::CreateDefault();
        ASSERT_NE(factory_, nullptr);
        ASSERT_NE(op_, nullptr);
    }

    std::shared_ptr<IBufferFactory> factory_;
    std::shared_ptr<IBufferOperator> op_;
};

TEST_F(BufferOperatorTest, Append) {
    auto buf = factory_->CreateBuffer(100);
    ASSERT_NE(buf, nullptr);
    const char* data = "Hello";
    BufferView view(data, 5);
    EXPECT_TRUE(op_->Append(*buf, view));
    EXPECT_EQ(buf->size(), 5);
    EXPECT_STREQ(buf->data(), "Hello");

    // 追加更多
    const char* more = " World";
    BufferView view2(more, 6);
    EXPECT_TRUE(op_->Append(*buf, view2));
    EXPECT_EQ(buf->size(), 11);
    EXPECT_STREQ(buf->data(), "Hello World");
}

TEST_F(BufferOperatorTest, AppendInsufficientSpace) {
    auto buf = factory_->CreateBuffer(5);
    ASSERT_NE(buf, nullptr);
    const char* data = "HelloWorld";
    BufferView view(data, 10);
    EXPECT_FALSE(op_->Append(*buf, view));
    EXPECT_EQ(buf->size(), 0);
}

TEST_F(BufferOperatorTest, Prepend) {
    auto buf = factory_->CreateBuffer(100);
    ASSERT_NE(buf, nullptr);
    const char* data = "World";
    BufferView view(data, 5);
    EXPECT_TRUE(op_->Prepend(*buf, view));
    EXPECT_EQ(buf->size(), 5);
    EXPECT_STREQ(buf->data(), "World");

    // 在前面再添加
    const char* more = "Hello ";
    BufferView view2(more, 6);
    EXPECT_TRUE(op_->Prepend(*buf, view2));
    EXPECT_EQ(buf->size(), 11);
    EXPECT_STREQ(buf->data(), "Hello World");
}

TEST_F(BufferOperatorTest, PrependInsufficientSpace) {
    auto buf = factory_->CreateBuffer(5);
    ASSERT_NE(buf, nullptr);
    const char* data = "HelloWorld";
    BufferView view(data, 10);
    EXPECT_FALSE(op_->Prepend(*buf, view));
    EXPECT_EQ(buf->size(), 0);
}

TEST_F(BufferOperatorTest, Merge) {
    std::vector<BufferView> views;
    const char* part1 = "Hello";
    const char* part2 = " ";
    const char* part3 = "World";
    views.emplace_back(part1, 5);
    views.emplace_back(part2, 1);
    views.emplace_back(part3, 5);
    auto merged = op_->Merge(views, factory_);
    ASSERT_NE(merged, nullptr);
    EXPECT_EQ(merged->size(), 11);
    EXPECT_STREQ(merged->data(), "Hello World");
}

TEST_F(BufferOperatorTest, Split) {
    auto buf = factory_->CreateBuffer(100);
    ASSERT_NE(buf, nullptr);
    const char* data = "HelloWorld";
    BufferView view(data, 10);
    op_->Append(*buf, view);
    auto split = op_->Split(*buf, 5, factory_);
    ASSERT_NE(split, nullptr);
    EXPECT_EQ(split->size(), 5);
    EXPECT_STREQ(split->data(), "World");
    // 原缓冲区不变（因为 const）
    EXPECT_EQ(buf->size(), 10);
    EXPECT_STREQ(buf->data(), "HelloWorld");
}

TEST_F(BufferOperatorTest, SplitAtBoundary) {
    auto buf = factory_->CreateBuffer(10);
    ASSERT_NE(buf, nullptr);
    const char* data = "HelloWorld";
    BufferView view(data, 10);
    op_->Append(*buf, view);
    auto split1 = op_->Split(*buf, 0, factory_);
    ASSERT_NE(split1, nullptr);
    EXPECT_EQ(split1->size(), 10);
    EXPECT_STREQ(split1->data(), "HelloWorld");
    auto split2 = op_->Split(*buf, 10, factory_);
    ASSERT_NE(split2, nullptr);
    EXPECT_EQ(split2->size(), 0);
    auto split3 = op_->Split(*buf, 11, factory_);
    EXPECT_EQ(split3, nullptr);
}

TEST_F(BufferOperatorTest, Copy) {
    auto dest = factory_->CreateBuffer(20);
    ASSERT_NE(dest, nullptr);
    const char* src = "Hello";
    BufferView view(src, 5);
    EXPECT_TRUE(op_->Copy(*dest, view, 0));
    EXPECT_EQ(dest->size(), 5);
    EXPECT_STREQ(dest->data(), "Hello");

    const char* src2 = "World";
    BufferView view2(src2, 5);
    EXPECT_TRUE(op_->Copy(*dest, view2, 5));
    EXPECT_EQ(dest->size(), 10);
    EXPECT_STREQ(dest->data(), "HelloWorld");

    // 超出容量
    EXPECT_FALSE(op_->Copy(*dest, view2, 18));
}

TEST_F(BufferOperatorTest, Fill) {
    auto buf = factory_->CreateBuffer(10);
    ASSERT_NE(buf, nullptr);
    const char* data = "Hello";
    BufferView view(data, 5);
    op_->Append(*buf, view);
    EXPECT_TRUE(op_->Fill(*buf, 0x41)); // 'A'
    // 整个缓冲区（容量10）被填充，但有效数据大小仍为5
    EXPECT_EQ(buf->size(), 5);
    for (size_t i = 0; i < buf->capacity(); ++i) {
        EXPECT_EQ(buf->data()[i], 'A');
    }
}

TEST_F(BufferOperatorTest, ResizeShrink) {
    auto buf = factory_->CreateBuffer(20);
    ASSERT_NE(buf, nullptr);
    const char* data = "HelloWorld";
    BufferView view(data, 10);
    op_->Append(*buf, view);
    EXPECT_TRUE(op_->Resize(*buf, 5, factory_));
    EXPECT_EQ(buf->size(), 5);
    EXPECT_EQ(buf->capacity(), 20); // 容量不变
    EXPECT_STREQ(buf->data(), "Hello");
}

TEST_F(BufferOperatorTest, ResizeExpand) {
    auto buf = factory_->CreateBuffer(10);
    ASSERT_NE(buf, nullptr);
    const char* data = "Hello";
    BufferView view(data, 5);
    op_->Append(*buf, view);
    EXPECT_TRUE(op_->Resize(*buf, 20, factory_));
    EXPECT_EQ(buf->size(), 5);
    EXPECT_GE(buf->capacity(), 20);
    EXPECT_STREQ(buf->data(), "Hello");
    // 验证可以继续写入
    const char* more = " World";
    BufferView view2(more, 6);
    EXPECT_TRUE(op_->Append(*buf, view2));
    EXPECT_EQ(buf->size(), 11);
    EXPECT_STREQ(buf->data(), "Hello World");
}

TEST_F(BufferOperatorTest, ResizeNoChange) {
    auto buf = factory_->CreateBuffer(10);
    ASSERT_NE(buf, nullptr);
    const char* data = "Hello";
    BufferView view(data, 5);
    op_->Append(*buf, view);
    EXPECT_TRUE(op_->Resize(*buf, 5, factory_));
    EXPECT_EQ(buf->size(), 5);
    EXPECT_EQ(buf->capacity(), 10);
    EXPECT_STREQ(buf->data(), "Hello");
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
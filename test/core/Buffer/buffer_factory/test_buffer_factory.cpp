#include "../../../../../include/httpserver/core/tcp_connections/Buffer/buffer_factory.hpp"
#include "../../../../../include/httpserver/core/tcp_connections/Buffer/buffer.hpp"
#include "../../../../../include/httpserver/core/tcp_connections/Buffer/buffer_view.hpp"
#include <gtest/gtest.h>
#include <cstring>
#include <memory>

using namespace httpserver::core;

class BufferFactoryTest : public ::testing::Test {
protected:
    void SetUp() override {
        factory_ = IBufferFactory::CreateDefault();
        ASSERT_NE(factory_, nullptr);
    }

    std::shared_ptr<IBufferFactory> factory_;
};

TEST_F(BufferFactoryTest, CreateBuffer) {
    auto buf = factory_->CreateBuffer(100);
    ASSERT_NE(buf, nullptr);
    EXPECT_EQ(buf->capacity(), 100);
    EXPECT_EQ(buf->size(), 0);
    EXPECT_NE(buf->data(), nullptr);
}

TEST_F(BufferFactoryTest, CreateBufferAligned) {
    const size_t alignments[] = {1, 2, 4, 8, 16, 32, 64, 128};
    for (size_t align : alignments) {
        auto buf = factory_->CreateBufferAligned(100, align);
        ASSERT_NE(buf, nullptr);
        uintptr_t addr = reinterpret_cast<uintptr_t>(buf->data());
        EXPECT_EQ(addr % align, 0);
    }
}

TEST_F(BufferFactoryTest, CreateZeroSizeBuffer) {
    auto buf = factory_->CreateBuffer(0);
    EXPECT_EQ(buf, nullptr);
}

TEST_F(BufferFactoryTest, CloneBuffer) {
    const char* original = "Hello, World!";
    BufferView view(original, strlen(original));
    auto cloned = factory_->CloneBuffer(view);
    ASSERT_NE(cloned, nullptr);
    EXPECT_EQ(cloned->size(), view.size);
    EXPECT_STREQ(cloned->data(), original);
}

TEST_F(BufferFactoryTest, CloneEmptyBuffer) {
    BufferView view;
    auto cloned = factory_->CloneBuffer(view);
    EXPECT_EQ(cloned, nullptr);
}

TEST_F(BufferFactoryTest, RecycleBuffer) {
    auto buf = factory_->CreateBuffer(200);
    ASSERT_NE(buf, nullptr);
    void* addr = buf->data();
    factory_->RecycleBuffer(std::move(buf));
    // 重新分配，验证可以继续使用
    auto new_buf = factory_->CreateBuffer(200);
    ASSERT_NE(new_buf, nullptr);
    EXPECT_NE(new_buf->data(), nullptr);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
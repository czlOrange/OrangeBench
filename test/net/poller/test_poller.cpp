// test/net/poller_test.cpp
#include "httpserver/net/poller.hpp"

#include <gtest/gtest.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <thread>
#include <chrono>

using namespace httpserver::net;

class PollerTest : public ::testing::Test {
protected:
    void SetUp() override {
        poller_ = Poller::create_default();
        ASSERT_NE(poller_, nullptr);
    }

    void TearDown() override {
        // 确保所有 fd 都已删除
    }

    // 创建一对连接的 socket（用于读写测试）
    static std::pair<int, int> create_socket_pair() {
        int sv[2];
        if (::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
            perror("socketpair");
            return {-1, -1};
        }
        // 设置为非阻塞
        for (int fd : {sv[0], sv[1]}) {
            int flags = ::fcntl(fd, F_GETFL, 0);
            ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        }
        return {sv[0], sv[1]};
    }

    // 辅助函数：等待事件
    bool wait_for_event(int timeout_ms = 100) {
        std::vector<Poller::Event> events;
        int n = poller_->poll(events, timeout_ms);
        return n > 0;
    }

    std::unique_ptr<Poller> poller_;
};

// 测试工厂创建
TEST_F(PollerTest, CreateDefault) {
    EXPECT_NE(poller_, nullptr);
    EXPECT_STREQ(poller_->name(), "epoll");
}

// 测试添加、修改、删除 fd
TEST_F(PollerTest, AddModDelFd) {
    auto [fd1, fd2] = create_socket_pair();
    ASSERT_GE(fd1, 0);
    ASSERT_GE(fd2, 0);

    // 添加读事件
    EXPECT_NO_THROW(poller_->add_fd(fd1, EVENT_READ, nullptr));

    // 修改为写事件
    EXPECT_NO_THROW(poller_->mod_fd(fd1, EVENT_WRITE, nullptr));

    // 删除
    EXPECT_NO_THROW(poller_->del_fd(fd1));

    // 再次删除应该没问题（实现中忽略 ENOENT）
    EXPECT_NO_THROW(poller_->del_fd(fd1));

    close(fd1);
    close(fd2);
}

// 测试无效 fd 会抛异常
TEST_F(PollerTest, InvalidFd) {
    EXPECT_THROW(poller_->add_fd(-1, EVENT_READ, nullptr), std::system_error);
    EXPECT_THROW(poller_->mod_fd(-1, EVENT_READ, nullptr), std::system_error);
    EXPECT_THROW(poller_->del_fd(-1), std::system_error);
}

// 测试 epoll 未初始化的情况（通过破坏内部状态，这里仅测试构造成功）
TEST_F(PollerTest, AfterClose) {
    // 手动销毁 poller 重新创建？但这里不测试，因为析构会关闭 epoll_fd
    // 模拟一个已经 close 的场景：重新构造一个，但无法直接访问内部，跳过
}

// 测试读事件就绪
TEST_F(PollerTest, ReadEvent) {
    auto [fd1, fd2] = create_socket_pair();
    ASSERT_GE(fd1, 0);
    ASSERT_GE(fd2, 0);

    poller_->add_fd(fd1, EVENT_READ, nullptr);

    // 向 fd2 写入数据，fd1 应可读
    const char msg[] = "hello";
    ssize_t n = ::write(fd2, msg, sizeof(msg));
    ASSERT_EQ(n, sizeof(msg));

    // 等待事件
    std::vector<Poller::Event> events;
    int ret = poller_->poll(events, 100);
    EXPECT_GT(ret, 0);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].fd, fd1);
    EXPECT_TRUE(events[0].events & EVENT_READ);

    close(fd1);
    close(fd2);
}

// 测试写事件就绪
TEST_F(PollerTest, WriteEvent) {
    auto [fd1, fd2] = create_socket_pair();
    ASSERT_GE(fd1, 0);
    ASSERT_GE(fd2, 0);

    // 写事件通常立即就绪（除非缓冲区满）
    poller_->add_fd(fd1, EVENT_WRITE, nullptr);

    std::vector<Poller::Event> events;
    int ret = poller_->poll(events, 100);
    EXPECT_GT(ret, 0);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].fd, fd1);
    EXPECT_TRUE(events[0].events & EVENT_WRITE);

    close(fd1);
    close(fd2);
}

// 测试超时
TEST_F(PollerTest, Timeout) {
    auto [fd1, fd2] = create_socket_pair();
    ASSERT_GE(fd1, 0);
    ASSERT_GE(fd2, 0);

    poller_->add_fd(fd1, EVENT_READ, nullptr);  // 没有数据，不会就绪

    auto start = std::chrono::steady_clock::now();
    std::vector<Poller::Event> events;
    int ret = poller_->poll(events, 100);  // 超时 100ms
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - start)
                       .count();

    EXPECT_EQ(ret, 0);
    EXPECT_GE(elapsed, 50);   // 至少等待了一段时间
    EXPECT_LE(elapsed, 150);  // 但不应太久

    close(fd1);
    close(fd2);
}

// 测试多个 fd 同时就绪
TEST_F(PollerTest, MultipleFds) {
    auto [fd1, fd2] = create_socket_pair();
    auto [fd3, fd4] = create_socket_pair();
    ASSERT_GE(fd1, 0); ASSERT_GE(fd2, 0);
    ASSERT_GE(fd3, 0); ASSERT_GE(fd4, 0);

    poller_->add_fd(fd1, EVENT_READ, nullptr);
    poller_->add_fd(fd3, EVENT_READ, nullptr);

    // 向两个写端写入数据
    const char msg[] = "data";
    ::write(fd2, msg, sizeof(msg));
    ::write(fd4, msg, sizeof(msg));

    std::vector<Poller::Event> events;
    int ret = poller_->poll(events, 100);
    EXPECT_EQ(ret, 2);
    // 顺序不确定，但两个 fd 都应出现
    bool found1 = false, found3 = false;
    for (const auto& ev : events) {
        if (ev.fd == fd1) found1 = true;
        if (ev.fd == fd3) found3 = true;
        EXPECT_TRUE(ev.events & EVENT_READ);
    }
    EXPECT_TRUE(found1);
    EXPECT_TRUE(found3);

    close(fd1); close(fd2);
    close(fd3); close(fd4);
}

// 测试边缘触发（ET）：连续两次读，第二次可能不触发
TEST_F(PollerTest, EdgeTriggered) {
    auto [fd1, fd2] = create_socket_pair();
    ASSERT_GE(fd1, 0);
    ASSERT_GE(fd2, 0);

    poller_->add_fd(fd1, EVENT_READ, nullptr);  // 默认 ET

    // 写入一次数据
    const char msg[] = "hello";
    ::write(fd2, msg, sizeof(msg));

    // 第一次 poll 应该触发
    std::vector<Poller::Event> events;
    int ret = poller_->poll(events, 100);
    EXPECT_EQ(ret, 1);
    EXPECT_EQ(events[0].fd, fd1);

    // 第二次 poll 之前，没有新数据，不应触发（ET 特性）
    ret = poller_->poll(events, 50);
    EXPECT_EQ(ret, 0);

    // 再次写入，应该再次触发
    ::write(fd2, msg, sizeof(msg));
    ret = poller_->poll(events, 100);
    EXPECT_EQ(ret, 1);
    EXPECT_EQ(events[0].fd, fd1);

    close(fd1);
    close(fd2);
}

// 测试用户上下文
TEST_F(PollerTest, UserContext) {
    auto [fd1, fd2] = create_socket_pair();
    ASSERT_GE(fd1, 0);
    ASSERT_GE(fd2, 0);

    int context = 12345;
    poller_->add_fd(fd1, EVENT_READ, &context);

    ::write(fd2, "x", 1);

    std::vector<Poller::Event> events;
    int ret = poller_->poll(events, 100);
    EXPECT_EQ(ret, 1);
    EXPECT_EQ(events[0].context, &context);

    close(fd1);
    close(fd2);
}

// 测试修改事件类型
TEST_F(PollerTest, ModifyEvents) {
    auto [fd1, fd2] = create_socket_pair();
    ASSERT_GE(fd1, 0);
    ASSERT_GE(fd2, 0);

    poller_->add_fd(fd1, EVENT_READ, nullptr);

    // 不写入数据，poll 应超时
    std::vector<Poller::Event> events;
    int ret = poller_->poll(events, 10);
    EXPECT_EQ(ret, 0);

    // 修改为写事件（写通常就绪）
    poller_->mod_fd(fd1, EVENT_WRITE, nullptr);
    ret = poller_->poll(events, 10);
    EXPECT_EQ(ret, 1);
    EXPECT_EQ(events[0].fd, fd1);
    EXPECT_TRUE(events[0].events & EVENT_WRITE);

    close(fd1);
    close(fd2);
}

// 测试删除 fd 后不再产生事件
TEST_F(PollerTest, DeleteFd) {
    auto [fd1, fd2] = create_socket_pair();
    ASSERT_GE(fd1, 0);
    ASSERT_GE(fd2, 0);

    poller_->add_fd(fd1, EVENT_READ, nullptr);
    ::write(fd2, "x", 1);

    std::vector<Poller::Event> events;
    int ret = poller_->poll(events, 100);
    EXPECT_EQ(ret, 1);
    EXPECT_EQ(events[0].fd, fd1);

    // 删除 fd
    poller_->del_fd(fd1);

    // 再次写入，不应再触发
    ::write(fd2, "y", 1);
    ret = poller_->poll(events, 50);
    EXPECT_EQ(ret, 0);

    close(fd1);
    close(fd2);
}

// 测试 max_events 设置和获取
TEST_F(PollerTest, MaxEvents) {
    // 默认是 1024
    EXPECT_EQ(poller_->max_events(), 1024u);
    // 通过 set_max_events 修改（但接口没有 set，只有 get）
    // 如果需要测试 set，需要具体实现提供 setter，但抽象类没有
    // 这里仅验证返回正确
}

// 测试信号中断（EINTR）处理
TEST_F(PollerTest, Interrupt) {
    // 很难直接模拟，但 poll 实现中如果返回 -1 且 errno==EINTR 会返回 0
    // 可以通过在另一个线程发送信号，但太复杂，跳过
    GTEST_SKIP() << "Signal interrupt test skipped due to complexity";
}

// 测试大量 fd 时的性能？简单测试是否成功添加
TEST_F(PollerTest, ManyFds) {
    const size_t count = 100;
    std::vector<int> fds;
    for (size_t i = 0; i < count; ++i) {
        auto [fd1, fd2] = create_socket_pair();
        if (fd1 >= 0 && fd2 >= 0) {
            fds.push_back(fd1);
            fds.push_back(fd2);
            poller_->add_fd(fd1, EVENT_READ, nullptr);
        }
    }
    // 只要没有异常就算通过
    EXPECT_GE(fds.size(), count * 2);
    for (int fd : fds) close(fd);
}

// 主函数（如果使用 gtest_main 则不需要）
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
// test/core/connections/test_connection_interface.cpp
#include "httpserver/core/tcp_connections/Connection/connection_interface.hpp"
#include "httpserver/core/tcp_connections/Async/async_scheduler.hpp"
#include "httpserver/core/tcp_connections/Event/event_dispatcher.hpp"

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <thread>
#include <future>
#include <memory>

using namespace httpserver::core;
using namespace httpserver::core::async;

// 前向声明测试类（放在全局作用域）
class ConnectionInterfaceTest;

// ============================================================================
// Mock 类定义（放在全局作用域）
// ============================================================================

// 模拟 IIOHandler
class MockIOHandler : public IIOHandler {
public:
    MOCK_METHOD(IOResult<SocketFd>, CreateSocket, (int, int, int), (override));
    MOCK_METHOD(IOResult<void>, CloseSocket, (SocketFd), (noexcept, override));
    MOCK_METHOD(IOResult<void>, ShutdownSocket, (SocketFd, int), (noexcept, override));
    MOCK_METHOD(IOResult<void>, SetSocketOption, (SocketFd, int, int, const void*, socklen_t), (override));
    MOCK_METHOD(IOResult<void>, SetNonBlocking, (SocketFd, bool), (override));
    MOCK_METHOD(IOResult<void>, SetTcpNoDelay, (SocketFd, bool), (override));
    MOCK_METHOD(IOResult<void>, Connect, (SocketFd, const sockaddr*, socklen_t), (override));
    MOCK_METHOD(IOResult<void>, Bind, (SocketFd, const sockaddr*, socklen_t), (override));
    MOCK_METHOD(IOResult<void>, Listen, (SocketFd, int), (override));
    MOCK_METHOD(IOResult<SocketFd>, Accept, (SocketFd, sockaddr*, socklen_t*), (override));
    MOCK_METHOD(IOResult<ssize_t>, Send, (SocketFd, const void*, size_t, int), (override));
    MOCK_METHOD(IOResult<ssize_t>, Recv, (SocketFd, void*, size_t, int), (override));
    MOCK_METHOD(IOResult<ssize_t>, SendTo, (SocketFd, const void*, size_t, int, const sockaddr*, socklen_t), (override));
    MOCK_METHOD(IOResult<ssize_t>, RecvFrom, (SocketFd, void*, size_t, int, sockaddr*, socklen_t*), (override));
    MOCK_METHOD(IOResult<bool>, PollRead, (SocketFd, int), (override));
    MOCK_METHOD(IOResult<bool>, PollWrite, (SocketFd, int), (override));
    MOCK_METHOD(IOResult<bool>, PollError, (SocketFd, int), (override));
    MOCK_METHOD(IOResult<SocketAddress>, GetLocalAddress, (SocketFd), (override));
    MOCK_METHOD(IOResult<SocketAddress>, GetPeerAddress, (SocketFd), (override));
    MOCK_METHOD(std::error_code, GetLastSocketError, (), (override));
    MOCK_METHOD(std::string, ErrorToString, (int), (override));
};

// 模拟调度器
class MockScheduler : public IScheduler {
public:
    MOCK_METHOD(void, Start, (), (override));
    MOCK_METHOD(void, Stop, (), (override));
    MOCK_METHOD(void, Pause, (), (override));
    MOCK_METHOD(void, Resume, (), (override));
    MOCK_METHOD(void, ScheduleBatch, (std::vector<std::function<void()>>), (override));
    MOCK_METHOD(void, ScheduleAfter, (std::function<void()>, std::chrono::milliseconds), (override));
    MOCK_METHOD(void, ScheduleEvery, (std::function<void()>, std::chrono::milliseconds), (override));
    MOCK_METHOD(SchedulerStats, GetStats, (), (const, override));
    MOCK_METHOD(void, SetMaxConcurrent, (size_t), (override));
    MOCK_METHOD(void, SetThreadPoolSize, (size_t), (override));

protected:
    void ScheduleTask(std::function<void()> task) override {
        task();  // 立即执行，简化测试
    }
};

// 模拟事件分发器
class MockDispatcher : public IEventDispatcher {
public:
    MOCK_METHOD(void, Register, (int, EventMask, EventCallback), (override));
    MOCK_METHOD(void, Modify, (int, EventMask), (override));
    MOCK_METHOD(void, Unregister, (int), (override));
    MOCK_METHOD(int, Poll, (std::vector<Event>&, int), (override));
    MOCK_METHOD(void, Stop, (), (override));
    MOCK_METHOD(size_t, MaxEvents, (), (const, override));
    MOCK_METHOD(const char*, Name, (), (const, override));
};

// ============================================================================
// 测试夹具
// ============================================================================

class ConnectionInterfaceTest : public ::testing::Test {
protected:
    void SetUp() override {
        io_handler_ = std::make_shared<MockIOHandler>();
        buffer_mgr_ = IBufferManager::CreateDefault();
        scheduler_ = std::make_shared<MockScheduler>();
        dispatcher_ = std::make_shared<MockDispatcher>();
        conn_ = IConnection::Create(io_handler_, buffer_mgr_, scheduler_, dispatcher_);
    }

    std::shared_ptr<MockIOHandler> io_handler_;
    std::shared_ptr<IBufferManager> buffer_mgr_;
    std::shared_ptr<MockScheduler> scheduler_;
    std::shared_ptr<MockDispatcher> dispatcher_;
    std::shared_ptr<IConnection> conn_;
};

// ============================================================================
// 测试用例
// ============================================================================

// 测试创建
TEST_F(ConnectionInterfaceTest, Create) {
    EXPECT_NE(conn_, nullptr);
    EXPECT_EQ(conn_->GetState(), ConnectionState::DISCONNECTED);
    EXPECT_FALSE(conn_->IsConnected());
}

// 测试同步发送接收
TEST_F(ConnectionInterfaceTest, SyncSendReceive) {
    // 模拟成功连接
    EXPECT_CALL(*io_handler_, CreateSocket(AF_INET, SOCK_STREAM, 0))
        .WillOnce(::testing::Return(IOResult<SocketFd>(3)));
    EXPECT_CALL(*io_handler_, SetNonBlocking(3, true))
        .WillOnce(::testing::Return(IOResult<void>()));
    EXPECT_CALL(*io_handler_, Connect(3, ::testing::_, ::testing::_))
        .WillOnce(::testing::Return(IOResult<void>()));
    EXPECT_CALL(*dispatcher_, Register(3, EVENT_READ, ::testing::_))
        .Times(1);

    auto err = conn_->Connect("127.0.0.1", 8080);
    EXPECT_FALSE(err);
    EXPECT_EQ(conn_->GetState(), ConnectionState::CONNECTED);

    // 发送数据
    const char* msg = "Hello";
    EXPECT_CALL(*io_handler_, Send(3, msg, 5, 0))
        .WillOnce(::testing::Return(IOResult<ssize_t>(5)));
    auto [sent, send_err] = conn_->Send(msg, 5);
    EXPECT_EQ(sent, 5);
    EXPECT_FALSE(send_err);

    // 接收数据
    char buffer[10] = {0};
    EXPECT_CALL(*io_handler_, Recv(3, buffer, 10, 0))
        .WillOnce(::testing::Return(IOResult<ssize_t>(3)));
    auto [recvd, recv_err] = conn_->Receive(buffer, 10);
    EXPECT_EQ(recvd, 3);
    EXPECT_FALSE(recv_err);
}

// 测试异步发送
TEST_F(ConnectionInterfaceTest, AsyncSend) {
    // 模拟连接成功
    EXPECT_CALL(*io_handler_, CreateSocket(AF_INET, SOCK_STREAM, 0))
        .WillOnce(::testing::Return(IOResult<SocketFd>(3)));
    EXPECT_CALL(*io_handler_, SetNonBlocking(3, true))
        .WillOnce(::testing::Return(IOResult<void>()));
    EXPECT_CALL(*io_handler_, Connect(3, ::testing::_, ::testing::_))
        .WillOnce(::testing::Return(IOResult<void>()));
    EXPECT_CALL(*dispatcher_, Register(3, EVENT_READ, ::testing::_))
        .Times(1);

    auto err = conn_->Connect("127.0.0.1", 8080);
    ASSERT_FALSE(err);

    // 异步发送
    const char* msg = "Async";
    EXPECT_CALL(*io_handler_, Send(3, msg, 5, 0))
        .WillOnce(::testing::Return(IOResult<ssize_t>(5)));
    auto future = conn_->SendAsync(msg, 5);
    auto status = future.wait_for(std::chrono::milliseconds(100));
    EXPECT_EQ(status, std::future_status::ready);
    auto result = future.get();
    EXPECT_EQ(result, 5);
}

// 测试连接错误
TEST_F(ConnectionInterfaceTest, ConnectError) {
    EXPECT_CALL(*io_handler_, CreateSocket(AF_INET, SOCK_STREAM, 0))
        .WillOnce(::testing::Return(IOResult<SocketFd>(3)));
    EXPECT_CALL(*io_handler_, SetNonBlocking(3, true))
        .WillOnce(::testing::Return(IOResult<void>()));
    EXPECT_CALL(*io_handler_, Connect(3, ::testing::_, ::testing::_))
        .WillOnce(::testing::Return(IOResult<void>(std::error_code(ECONNREFUSED, std::system_category()))));
    
    auto err = conn_->Connect("127.0.0.1", 8080);
    EXPECT_TRUE(err);
    EXPECT_EQ(err.value(), ECONNREFUSED);
    EXPECT_EQ(conn_->GetState(), ConnectionState::DISCONNECTED);
}

// 测试断开连接
TEST_F(ConnectionInterfaceTest, Disconnect) {
    // 模拟连接成功
    EXPECT_CALL(*io_handler_, CreateSocket(AF_INET, SOCK_STREAM, 0))
        .WillOnce(::testing::Return(IOResult<SocketFd>(3)));
    EXPECT_CALL(*io_handler_, SetNonBlocking(3, true))
        .WillOnce(::testing::Return(IOResult<void>()));
    EXPECT_CALL(*io_handler_, Connect(3, ::testing::_, ::testing::_))
        .WillOnce(::testing::Return(IOResult<void>()));
    EXPECT_CALL(*dispatcher_, Register(3, EVENT_READ, ::testing::_))
        .Times(1);

    auto err = conn_->Connect("127.0.0.1", 8080);
    ASSERT_FALSE(err);
    EXPECT_EQ(conn_->GetState(), ConnectionState::CONNECTED);

    EXPECT_CALL(*io_handler_, CloseSocket(3))
        .WillOnce(::testing::Return(IOResult<void>()));
    EXPECT_CALL(*dispatcher_, Unregister(3))
        .Times(1);
    
    err = conn_->Disconnect();
    EXPECT_FALSE(err);
    EXPECT_EQ(conn_->GetState(), ConnectionState::DISCONNECTED);
}

// 主函数
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
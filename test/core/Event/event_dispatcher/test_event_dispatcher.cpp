// test/core/tcp_connections/Event/event_dispatcher_test.cpp
#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <atomic>
#include "httpserver/core/tcp_connections/Event/event_dispatcher.hpp"

using namespace httpserver::core;

// ============================================================================
// 模拟连接类
// ============================================================================
class MockConnection : public IConnection {
public:
    MockConnection(uint64_t id = 0) : id_(id) {}

    // 测试需要的方法
    uint64_t GetConnectionId() const noexcept override { return id_; }
    void SetConnectionId(uint64_t id) override { id_ = id; }
    
    // 其他方法返回默认值（最小实现）
    std::error_code Connect(const std::string&, uint16_t) override { return {}; }
    std::error_code Connect(const SocketAddress&) override { return {}; }
    std::error_code Disconnect() noexcept override { return {}; }
    void Close() noexcept override {}
    size_t Send(std::string_view) override { return 0; }
    size_t Send(const void*, size_t) override { return 0; }
    std::string Receive(size_t) override { return {}; }
    size_t Receive(void*, size_t) override { return 0; }
    AsyncResult<size_t> SendAsync(std::string_view) override { return {}; }
    AsyncResult<size_t> SendAsync(const void*, size_t) override { return {}; }
    AsyncResult<std::string> ReceiveAsync(size_t) override { return {}; }
    AsyncResult<size_t> ReceiveAsync(void*, size_t) override { return {}; }
    std::error_code Flush() override { return {}; }
    void ClearBuffers() noexcept override {}
    size_t GetPendingSendBytes() const noexcept override { return 0; }
    size_t GetPendingReceiveBytes() const noexcept override { return 0; }
    void Configure(const ConnectionConfig&) override {}
    ConnectionConfig GetConfig() const override { return {}; }
    void UpdateConfig(std::function<void(ConnectionConfig&)>) override {}
    ConnectionState GetState() const override { return ConnectionState::DISCONNECTED; }
    bool IsConnected() const override { return false; }
    bool IsReadable() const override { return false; }
    bool IsWritable() const override { return false; }
    bool HasError() const override { return false; }
    std::string GetLocalAddress() const override { return {}; }
    std::string GetPeerAddress() const override { return {}; }
    uint16_t GetLocalPort() const noexcept override { return 0; }
    uint16_t GetPeerPort() const noexcept override { return 0; }
    SocketAddress GetLocalSocketAddress() const override { return {}; }
    SocketAddress GetPeerSocketAddress() const override { return {}; }
    size_t GetBytesSent() const noexcept override { return 0; }
    size_t GetBytesReceived() const noexcept override { return 0; }
    size_t GetTotalOperations() const noexcept override { return 0; }
    std::chrono::steady_clock::time_point GetConnectTime() const override { return {}; }
    std::chrono::steady_clock::time_point GetLastActivityTime() const override { return {}; }
    bool WaitForData(std::chrono::milliseconds) override { return false; }
    bool WaitForWritable(std::chrono::milliseconds) override { return false; }
    int GetFd() const override { return -1; }
    void SetEventCallback(EventCallback) override {}
    void SetDataCallback(DataCallback) override {}
    void SetErrorCallback(ErrorCallback) override {}
    void SetConnectionManager(std::shared_ptr<IConnectionManager>) override {}
    std::shared_ptr<IConnectionManager> GetConnectionManager() const override { return nullptr; }
    void SetUserData(const std::string&, std::any) override {}
    std::any GetUserData(const std::string&) const override { return {}; }
    bool HasUserData(const std::string&) const override { return false; }
    void RemoveUserData(const std::string&) override {}
    void UpdateHeartbeat() override {}
    bool IsHeartbeatExpired(std::chrono::milliseconds) const override { return false; }

private:
    uint64_t id_;
};

// ============================================================================
// 测试订阅者类
// ============================================================================
class TestSubscriber : public IEventSubscriber {
public:
    TestSubscriber(std::string id = "test")
        : id_(std::move(id))
        , event_called_(false)
        , last_event_(ConnectionEvent::CONNECTED)
        , data_received_("")
        , error_code_() {}

    void OnEvent(ConnectionEvent event, std::shared_ptr<IConnection> conn) override {
        event_called_ = true;
        last_event_ = event;
        last_conn_ = conn;
    }

    void OnData(std::shared_ptr<IConnection> conn, std::string_view data) override {
        data_called_ = true;
        data_received_ = std::string(data);
        data_conn_ = conn;
    }

    void OnError(std::shared_ptr<IConnection> conn, std::error_code ec) override {
        error_called_ = true;
        error_code_ = ec;
        error_conn_ = conn;
    }

    std::string GetSubscriberId() const override { return id_; }

    bool event_called_ = false;
    ConnectionEvent last_event_;
    std::shared_ptr<IConnection> last_conn_;
    bool data_called_ = false;
    std::string data_received_;
    std::shared_ptr<IConnection> data_conn_;
    bool error_called_ = false;
    std::error_code error_code_;
    std::shared_ptr<IConnection> error_conn_;

private:
    std::string id_;
};

// ============================================================================
// 测试套件
// ============================================================================

class EventDispatcherTest : public ::testing::Test {
protected:
    void SetUp() override {
        dispatcher_ = IEventDispatcher::CreateDefault();
        conn_ = std::make_shared<MockConnection>(123);
    }

    std::shared_ptr<IEventDispatcher> dispatcher_;
    std::shared_ptr<MockConnection> conn_;
};

// ============================================================================
// 基础功能测试
// ============================================================================

TEST_F(EventDispatcherTest, CreateDefault) {
    EXPECT_NE(dispatcher_, nullptr);
}

TEST_F(EventDispatcherTest, PublishEventWithCallback) {
    bool called = false;
    dispatcher_->SubscribeEvent(ConnectionEvent::CONNECTED,
        [&called](ConnectionEvent event, std::shared_ptr<IConnection> conn) {
            called = true;
            EXPECT_EQ(event, ConnectionEvent::CONNECTED);
        });

    dispatcher_->PublishEvent(ConnectionEvent::CONNECTED, conn_);
    EXPECT_TRUE(called);
}

TEST_F(EventDispatcherTest, PublishDataWithCallback) {
    std::string received;
    dispatcher_->SubscribeData(
        [&received](std::shared_ptr<IConnection> conn, std::string_view data) {
            received = std::string(data);
        });

    dispatcher_->PublishData(conn_, "hello world");
    EXPECT_EQ(received, "hello world");
}

TEST_F(EventDispatcherTest, PublishErrorWithCallback) {
    std::error_code received;
    dispatcher_->SubscribeError(
        [&received](std::shared_ptr<IConnection> conn, std::error_code ec) {
            received = ec;
        });

    std::error_code test_ec = make_error_code(std::errc::connection_refused);
    dispatcher_->PublishError(conn_, test_ec);
    EXPECT_EQ(received, test_ec);
}

// ============================================================================
// 订阅者对象测试
// ============================================================================

TEST_F(EventDispatcherTest, RegisterSubscriber) {
    auto sub = std::make_shared<TestSubscriber>("sub1");
    dispatcher_->RegisterSubscriber(sub);

    dispatcher_->PublishEvent(ConnectionEvent::CONNECTED, conn_);
    EXPECT_TRUE(sub->event_called_);
    EXPECT_EQ(sub->last_event_, ConnectionEvent::CONNECTED);
    EXPECT_EQ(sub->last_conn_, conn_);
}

TEST_F(EventDispatcherTest, UnregisterSubscriber) {
    auto sub = std::make_shared<TestSubscriber>("sub2");
    dispatcher_->RegisterSubscriber(sub);
    dispatcher_->UnregisterSubscriber("sub2");

    dispatcher_->PublishEvent(ConnectionEvent::CONNECTED, conn_);
    EXPECT_FALSE(sub->event_called_);
}

TEST_F(EventDispatcherTest, MultipleSubscribers) {
    auto sub1 = std::make_shared<TestSubscriber>("sub1");
    auto sub2 = std::make_shared<TestSubscriber>("sub2");
    dispatcher_->RegisterSubscriber(sub1);
    dispatcher_->RegisterSubscriber(sub2);

    dispatcher_->PublishData(conn_, "test");
    EXPECT_TRUE(sub1->data_called_);
    EXPECT_TRUE(sub2->data_called_);
    EXPECT_EQ(sub1->data_received_, "test");
    EXPECT_EQ(sub2->data_received_, "test");
}

// ============================================================================
// 事件过滤测试
// ============================================================================

TEST_F(EventDispatcherTest, EventFilter) {
    bool called = false;
    dispatcher_->SubscribeEvent(ConnectionEvent::CONNECTED,
        [&called](auto, auto) { called = true; });

    // 添加过滤器，丢弃所有事件
    dispatcher_->AddEventFilter([](auto, auto) { return false; });

    dispatcher_->PublishEvent(ConnectionEvent::CONNECTED, conn_);
    EXPECT_FALSE(called);
}

TEST_F(EventDispatcherTest, MultipleFilters) {
    int filter_count = 0;
    dispatcher_->AddEventFilter([&filter_count](auto, auto) { filter_count++; return true; });
    dispatcher_->AddEventFilter([&filter_count](auto, auto) { filter_count++; return true; });

    dispatcher_->PublishEvent(ConnectionEvent::CONNECTED, conn_);
    EXPECT_EQ(filter_count, 2);
}

// ============================================================================
// 事件路由测试
// ============================================================================

TEST_F(EventDispatcherTest, RouteEvent) {
    auto sub = std::make_shared<TestSubscriber>("target");
    dispatcher_->RegisterSubscriber(sub);

    dispatcher_->RouteEvent(ConnectionEvent::CONNECTED, conn_, "target");
    EXPECT_TRUE(sub->event_called_);
    EXPECT_EQ(sub->last_event_, ConnectionEvent::CONNECTED);
}

TEST_F(EventDispatcherTest, RouteEventToNonExistent) {
    auto sub = std::make_shared<TestSubscriber>("target");
    dispatcher_->RegisterSubscriber(sub);

    // 路由到不存在的ID，不应调用
    dispatcher_->RouteEvent(ConnectionEvent::CONNECTED, conn_, "nonexistent");
    EXPECT_FALSE(sub->event_called_);
}

// ============================================================================
// 统计信息测试
// ============================================================================

TEST_F(EventDispatcherTest, GetStats) {
    auto stats = dispatcher_->GetStats();
    EXPECT_EQ(stats.total_events, 0);
    EXPECT_EQ(stats.total_data, 0);
    EXPECT_EQ(stats.total_errors, 0);
    EXPECT_EQ(stats.active_subscribers, 0);

    dispatcher_->PublishEvent(ConnectionEvent::CONNECTED, conn_);
    dispatcher_->PublishData(conn_, "test");
    dispatcher_->PublishError(conn_, std::error_code());

    auto sub = std::make_shared<TestSubscriber>();
    dispatcher_->RegisterSubscriber(sub);

    stats = dispatcher_->GetStats();
    EXPECT_EQ(stats.total_events, 1);
    EXPECT_EQ(stats.total_data, 1);
    EXPECT_EQ(stats.total_errors, 1);
    EXPECT_EQ(stats.active_subscribers, 1);
}

// ============================================================================
// 配置测试
// ============================================================================

TEST_F(EventDispatcherTest, SetMaxQueueSize) {
    EXPECT_NO_THROW(dispatcher_->SetMaxQueueSize(100));
}

TEST_F(EventDispatcherTest, SetAsyncDispatch) {
    EXPECT_NO_THROW(dispatcher_->SetAsyncDispatch(true));
    EXPECT_NO_THROW(dispatcher_->SetAsyncDispatch(false));
}

// ============================================================================
// 多线程测试
// ============================================================================

TEST_F(EventDispatcherTest, ConcurrentPublish) {
    const int thread_count = 10;
    const int events_per_thread = 100;
    std::atomic<int> received_count{0};

    dispatcher_->SubscribeEvent(ConnectionEvent::CONNECTED,
        [&received_count](auto, auto) { received_count++; });

    std::vector<std::thread> threads;
    for (int i = 0; i < thread_count; ++i) {
        threads.emplace_back([this, events_per_thread]() {
            for (int j = 0; j < events_per_thread; ++j) {
                dispatcher_->PublishEvent(ConnectionEvent::CONNECTED, conn_);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // 等待异步分发完成（如果启用异步）
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    EXPECT_EQ(received_count, thread_count * events_per_thread);
}

// ============================================================================
// 混合订阅测试（回调 + 订阅者对象）
// ============================================================================

TEST_F(EventDispatcherTest, MixedSubscribers) {
    bool callback_called = false;
    auto sub = std::make_shared<TestSubscriber>("mixed");

    dispatcher_->SubscribeEvent(ConnectionEvent::CONNECTED,
        [&callback_called](auto, auto) { callback_called = true; });
    dispatcher_->RegisterSubscriber(sub);

    dispatcher_->PublishEvent(ConnectionEvent::CONNECTED, conn_);

    EXPECT_TRUE(callback_called);
    EXPECT_TRUE(sub->event_called_);
}

// ============================================================================
// 主函数
// ============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
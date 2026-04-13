#include "../../../../include/httpserver/core/tcp_connections/IO/io_handler.hpp"
#include <gtest/gtest.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <thread>
#include <chrono>

using namespace httpserver::core;

// ============================================================================
// Result 类型测试
// ============================================================================

TEST(ResultTest, SuccessValue) {
    Result<int> res(42);
    EXPECT_TRUE(res.has_value());
    EXPECT_FALSE(res.has_error());
    EXPECT_EQ(res.value(), 42);
    EXPECT_EQ(*res, 42);
    EXPECT_TRUE(static_cast<bool>(res));
}

TEST(ResultTest, Error) {
    std::error_code ec(EPERM, std::system_category());
    Result<int> res(ec);
    EXPECT_FALSE(res.has_value());
    EXPECT_TRUE(res.has_error());
    EXPECT_EQ(res.error(), ec);
    EXPECT_FALSE(static_cast<bool>(res));
}

TEST(ResultTest, VoidSuccess) {
    Result<void> res;
    EXPECT_TRUE(res.has_value());
    EXPECT_FALSE(res.has_error());
    EXPECT_NO_THROW(res.value());
}

TEST(ResultTest, VoidError) {
    std::error_code ec(EPERM, std::system_category());
    Result<void> res(ec);
    EXPECT_FALSE(res.has_value());
    EXPECT_TRUE(res.has_error());
    EXPECT_EQ(res.error(), ec);
    EXPECT_THROW(res.value(), std::system_error);
}

// ============================================================================
// SocketAddress 测试
// ============================================================================

TEST(SocketAddressTest, FromIpPort) {
    auto addr = SocketAddress::FromIpPort("192.168.1.1", 8080);
    EXPECT_EQ(addr.GetIp(), "192.168.1.1");
    EXPECT_EQ(addr.GetPort(), 8080);
    EXPECT_EQ(addr.ToString(), "192.168.1.1:8080");
}

TEST(SocketAddressTest, FromAnyAddress) {
    auto addr = SocketAddress::FromIpPort("0.0.0.0", 9090);
    EXPECT_EQ(addr.GetIp(), "0.0.0.0");
    EXPECT_EQ(addr.GetPort(), 9090);
    EXPECT_EQ(addr.ToString(), "0.0.0.0:9090");
}

TEST(SocketAddressTest, FromEmptyIp) {
    auto addr = SocketAddress::FromIpPort("", 1234);
    EXPECT_EQ(addr.GetIp(), "0.0.0.0");
    EXPECT_EQ(addr.GetPort(), 1234);
}

TEST(SocketAddressTest, FromInvalidIp) {
    auto addr = SocketAddress::FromIpPort("invalid", 1234);
    EXPECT_EQ(addr.GetIp(), "0.0.0.0");
    EXPECT_EQ(addr.GetPort(), 1234);
}

// ============================================================================
// PosixIOHandler 测试
// ============================================================================

class IOHandlerTest : public ::testing::Test {
protected:
    void SetUp() override {
        handler_ = IIOHandler::CreateDefault();
        ASSERT_NE(handler_, nullptr);
    }

    void TearDown() override {
        // 清理所有可能残留的 fd
        for (int fd : open_fds_) {
            if (fd >= 0) {
                handler_->CloseSocket(fd);
            }
        }
        open_fds_.clear();
    }

    // 辅助：创建 TCP 监听 socket
    IOResult<SocketFd> CreateListenSocket(uint16_t port) {
        auto fd_res = handler_->CreateSocket(AF_INET, SOCK_STREAM, 0);
        if (!fd_res.has_value()) {
            return fd_res.error();
        }
        int fd = fd_res.value();
        open_fds_.push_back(fd);

        // 设置重用地址
        int reuse = 1;
        auto opt_res = handler_->SetSocketOption(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        if (opt_res.has_error()) {
            handler_->CloseSocket(fd);
            return opt_res.error();
        }

        auto addr = SocketAddress::FromIpPort("127.0.0.1", port);
        auto bind_res = handler_->Bind(fd, (sockaddr*)&addr.addr, addr.len);
        if (bind_res.has_error()) {
            handler_->CloseSocket(fd);
            return bind_res.error();
        }

        auto listen_res = handler_->Listen(fd, SOMAXCONN);
        if (listen_res.has_error()) {
            handler_->CloseSocket(fd);
            return listen_res.error();
        }
        return fd;
    }

    // 辅助：创建客户端连接 socket
    IOResult<SocketFd> CreateClientSocket(uint16_t port) {
        auto fd_res = handler_->CreateSocket(AF_INET, SOCK_STREAM, 0);
        if (!fd_res.has_value()) {
            return fd_res.error();
        }
        int fd = fd_res.value();
        open_fds_.push_back(fd);

        auto addr = SocketAddress::FromIpPort("127.0.0.1", port);
        auto connect_res = handler_->Connect(fd, (sockaddr*)&addr.addr, addr.len);
        if (connect_res.has_error()) {
            handler_->CloseSocket(fd);
            return connect_res.error();
        }
        return fd;
    }

    std::shared_ptr<IIOHandler> handler_;
    std::vector<int> open_fds_;
};

TEST_F(IOHandlerTest, CreateSocket) {
    auto res = handler_->CreateSocket(AF_INET, SOCK_STREAM, 0);
    ASSERT_TRUE(res.has_value());
    int fd = res.value();
    EXPECT_GE(fd, 0);
    handler_->CloseSocket(fd);
}

TEST_F(IOHandlerTest, CloseSocket) {
    auto res = handler_->CreateSocket(AF_INET, SOCK_STREAM, 0);
    ASSERT_TRUE(res.has_value());
    int fd = res.value();
    auto close_res = handler_->CloseSocket(fd);
    EXPECT_TRUE(close_res.has_value());
    // 从列表中移除，避免重复关闭
    open_fds_.erase(std::remove(open_fds_.begin(), open_fds_.end(), fd), open_fds_.end());
}

TEST_F(IOHandlerTest, SetNonBlocking) {
    auto res = handler_->CreateSocket(AF_INET, SOCK_STREAM, 0);
    ASSERT_TRUE(res.has_value());
    int fd = res.value();
    auto set_res = handler_->SetNonBlocking(fd, true);
    EXPECT_TRUE(set_res.has_value());

    // 验证非阻塞标志已设置
    int flags = fcntl(fd, F_GETFL, 0);
    EXPECT_TRUE(flags & O_NONBLOCK);
    handler_->CloseSocket(fd);
}

TEST_F(IOHandlerTest, SetTcpNoDelay) {
    auto res = handler_->CreateSocket(AF_INET, SOCK_STREAM, 0);
    ASSERT_TRUE(res.has_value());
    int fd = res.value();
    auto set_res = handler_->SetTcpNoDelay(fd, true);
    EXPECT_TRUE(set_res.has_value());

    // 验证选项已设置（通过 getsockopt 检查）
    int val = 0;
    socklen_t len = sizeof(val);
    int ret = getsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &val, &len);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(val, 1);
    handler_->CloseSocket(fd);
}

TEST_F(IOHandlerTest, BindAndListen) {
    auto listen_res = CreateListenSocket(0);  // 端口 0 让系统自动分配
    ASSERT_TRUE(listen_res.has_value());
    int listen_fd = listen_res.value();

    // 获取实际绑定的地址
    auto addr_res = handler_->GetLocalAddress(listen_fd);
    ASSERT_TRUE(addr_res.has_value());
    auto addr = addr_res.value();
    EXPECT_EQ(addr.GetIp(), "127.0.0.1");
    EXPECT_GT(addr.GetPort(), 0);
}

TEST_F(IOHandlerTest, AcceptConnection) {
    // 服务端
    auto listen_res = CreateListenSocket(0);
    ASSERT_TRUE(listen_res.has_value());
    int listen_fd = listen_res.value();
    auto addr_res = handler_->GetLocalAddress(listen_fd);
    ASSERT_TRUE(addr_res.has_value());
    uint16_t port = addr_res.value().GetPort();

    // 客户端连接（在另一个线程）
    std::thread client_thread([this, port]() {
        auto client_res = CreateClientSocket(port);
        ASSERT_TRUE(client_res.has_value());
        int client_fd = client_res.value();
        // 稍等片刻，让服务端 accept
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        handler_->CloseSocket(client_fd);
    });

    // 服务端 accept
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    auto accept_res = handler_->Accept(listen_fd, (sockaddr*)&client_addr, &client_len);
    ASSERT_TRUE(accept_res.has_value());
    int client_fd = accept_res.value();
    EXPECT_GE(client_fd, 0);
    handler_->CloseSocket(client_fd);

    client_thread.join();
}

TEST_F(IOHandlerTest, SendAndReceive) {
    // 创建一对连接的 socket（使用 socketpair）
    int sv[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);
    open_fds_.push_back(sv[0]);
    open_fds_.push_back(sv[1]);

    // 发送数据
    const char* msg = "hello";
    auto send_res = handler_->Send(sv[0], msg, strlen(msg), 0);
    ASSERT_TRUE(send_res.has_value());
    EXPECT_EQ(send_res.value(), static_cast<ssize_t>(strlen(msg)));

    // 接收数据
    char buffer[128] = {0};
    auto recv_res = handler_->Recv(sv[1], buffer, sizeof(buffer), 0);
    ASSERT_TRUE(recv_res.has_value());
    EXPECT_EQ(recv_res.value(), static_cast<ssize_t>(strlen(msg)));
    EXPECT_STREQ(buffer, msg);
}

TEST_F(IOHandlerTest, PollRead) {
    int sv[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);
    open_fds_.push_back(sv[0]);
    open_fds_.push_back(sv[1]);

    // 无数据时 poll 应超时
    auto poll_res = handler_->PollRead(sv[0], 10);
    ASSERT_TRUE(poll_res.has_value());
    EXPECT_FALSE(poll_res.value());  // 超时，无数据

    // 写入数据
    const char* msg = "data";
    handler_->Send(sv[1], msg, strlen(msg), 0);

    // poll 应检测到可读
    poll_res = handler_->PollRead(sv[0], 100);
    ASSERT_TRUE(poll_res.has_value());
    EXPECT_TRUE(poll_res.value());
}

TEST_F(IOHandlerTest, PollWrite) {
    int sv[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);
    open_fds_.push_back(sv[0]);
    open_fds_.push_back(sv[1]);

    // 通常写就绪
    auto poll_res = handler_->PollWrite(sv[0], 10);
    ASSERT_TRUE(poll_res.has_value());
    EXPECT_TRUE(poll_res.value());  // 通常可写
}

TEST_F(IOHandlerTest, GetLocalAndPeerAddress) {
    // 创建监听 socket
    auto listen_res = CreateListenSocket(0);
    ASSERT_TRUE(listen_res.has_value());
    int listen_fd = listen_res.value();
    auto addr_res = handler_->GetLocalAddress(listen_fd);
    ASSERT_TRUE(addr_res.has_value());
    uint16_t port = addr_res.value().GetPort();

    // 客户端连接
    auto client_res = CreateClientSocket(port);
    ASSERT_TRUE(client_res.has_value());
    int client_fd = client_res.value();

    // 服务端 accept
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    auto accept_res = handler_->Accept(listen_fd, (sockaddr*)&client_addr, &client_len);
    ASSERT_TRUE(accept_res.has_value());
    int server_fd = accept_res.value();
    open_fds_.push_back(server_fd);

    // 检查地址
    auto server_local = handler_->GetLocalAddress(server_fd);
    auto server_peer = handler_->GetPeerAddress(server_fd);
    auto client_local = handler_->GetLocalAddress(client_fd);
    auto client_peer = handler_->GetPeerAddress(client_fd);

    ASSERT_TRUE(server_local.has_value());
    ASSERT_TRUE(server_peer.has_value());
    ASSERT_TRUE(client_local.has_value());
    ASSERT_TRUE(client_peer.has_value());

    // 服务端的本地地址应等于客户端的对端地址
    EXPECT_EQ(server_local.value().GetIp(), client_peer.value().GetIp());
    EXPECT_EQ(server_local.value().GetPort(), client_peer.value().GetPort());
    // 服务端的对端地址应等于客户端的本地地址
    EXPECT_EQ(server_peer.value().GetIp(), client_local.value().GetIp());
    EXPECT_EQ(server_peer.value().GetPort(), client_local.value().GetPort());
}

TEST_F(IOHandlerTest, ErrorHandling) {
    // 无效 fd 操作应返回错误
    int invalid_fd = -1;
    auto res = handler_->Send(invalid_fd, "a", 1, 0);
    EXPECT_TRUE(res.has_error());
    EXPECT_EQ(res.error().value(), EBADF);  // 或 EINVAL? 实际上 close(-1) 会返回 EBADF
}

// 主函数（如果使用 gtest_main 则不需要）
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
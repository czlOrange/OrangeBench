// ============================================================================
// 文件: tests/socket_test.cpp
// 描述: Socket 单元测试（修复版）
// ============================================================================

#include <gtest/gtest.h>
#include "../../../include/httpserver/net/socket.hpp"
#include "../../../include/httpserver/net/address.hpp"
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>

using namespace httpserver::net;

// ============================================================================
// 辅助函数
// ============================================================================

template<typename F>
bool wait_for_condition(F condition, int timeout_ms = 1000) {
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - start).count() < timeout_ms) {
        if (condition()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return condition();
}

// ============================================================================
// 基础创建测试
// ============================================================================

TEST(SocketTest, CreateTCPSocket) {
    auto sock = create_socket(SocketType::TCP);
    EXPECT_TRUE(sock->is_valid());
    EXPECT_EQ(sock->type(), SocketType::TCP);
    EXPECT_GE(sock->fd(), 0);
}

TEST(SocketTest, CreateUDPSocket) {
    auto sock = create_socket(SocketType::UDP);
    EXPECT_TRUE(sock->is_valid());
    EXPECT_EQ(sock->type(), SocketType::UDP);
    EXPECT_GE(sock->fd(), 0);
}

TEST(SocketTest, CreateUnixStreamSocket) {
    EXPECT_THROW(create_socket(SocketType::UNIX_STREAM), std::runtime_error);
}

TEST(SocketTest, CreateUnixDgramSocket) {
    EXPECT_THROW(create_socket(SocketType::UNIX_DGRAM), std::runtime_error);
}

TEST(SocketTest, CreateFromInvalidFD) {
    EXPECT_THROW(create_socket_from_fd(-1, SocketType::TCP), std::invalid_argument);
}

// ============================================================================
// 地址绑定测试
// ============================================================================

TEST(SocketTest, BindToAnyAddress) {
    auto sock = create_socket(SocketType::TCP);
    NetAddress addr("0.0.0.0", 0);
    
    EXPECT_TRUE(sock->bind(addr));
    
    auto local_addr = sock->local_address();
    EXPECT_GT(local_addr.port(), 0);
    EXPECT_EQ(local_addr.ip(), "0.0.0.0");
}

TEST(SocketTest, BindToLocalhost) {
    auto sock = create_socket(SocketType::TCP);
    NetAddress addr("127.0.0.1", 12345);
    
    EXPECT_TRUE(sock->bind(addr));
    
    auto local_addr = sock->local_address();
    EXPECT_EQ(local_addr.port(), 12345);
    EXPECT_EQ(local_addr.ip(), "127.0.0.1");
}

TEST(SocketTest, BindTwiceError) {
    auto sock1 = create_socket(SocketType::TCP);
    auto sock2 = create_socket(SocketType::TCP);
    
    NetAddress addr("127.0.0.1", 12346);
    
    EXPECT_TRUE(sock1->bind(addr));
    EXPECT_FALSE(sock2->bind(addr));
    EXPECT_NE(sock2->get_error(), 0);
}

TEST(SocketTest, BindInvalidAddress) {
    auto sock = create_socket(SocketType::TCP);
    NetAddress addr("999.999.999.999", 80);
    
    EXPECT_FALSE(sock->bind(addr));
}

// ============================================================================
// TCP 连接测试
// ============================================================================

TEST(SocketTest, TCPListenAndAccept) {
    auto server = create_socket(SocketType::TCP);
    NetAddress listen_addr("127.0.0.1", 0);
    
    ASSERT_TRUE(server->bind(listen_addr));
    ASSERT_TRUE(server->listen(5));
    
    auto server_addr = server->local_address();
    ASSERT_GT(server_addr.port(), 0);
    
    std::atomic<bool> accepted{false};
    std::unique_ptr<Socket> client_sock;
    
    std::thread accept_thread([&server, &accepted, &client_sock]() {
        NetAddress peer_addr;
        client_sock = server->accept(&peer_addr);
        accepted = (client_sock != nullptr);
    });
    
    auto client = create_socket(SocketType::TCP);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    EXPECT_TRUE(client->connect(server_addr));
    accept_thread.join();
    EXPECT_TRUE(accepted);
    EXPECT_TRUE(client_sock->is_valid());
}

TEST(SocketTest, TCPConnectRefused) {
    auto client = create_socket(SocketType::TCP);
    NetAddress addr("127.0.0.1", 9999);
    
    EXPECT_FALSE(client->connect(addr));
    EXPECT_NE(client->get_error(), 0);
}

TEST(SocketTest, TCPListenWithoutBind) {
    auto sock = create_socket(SocketType::TCP);
    // Linux 允许未绑定的 socket 监听（自动绑定）
    EXPECT_TRUE(sock->listen(1024));
}

// ============================================================================
// 数据读写测试
// ============================================================================

TEST(SocketTest, TCPSendAndReceive) {
    auto server = create_socket(SocketType::TCP);
    NetAddress listen_addr("127.0.0.1", 0);
    
    ASSERT_TRUE(server->bind(listen_addr));
    ASSERT_TRUE(server->listen(5));
    
    auto server_addr = server->local_address();
    
    std::atomic<bool> accepted{false};
    std::unique_ptr<Socket> client_sock;
    
    std::thread accept_thread([&server, &accepted, &client_sock]() {
        NetAddress peer_addr;
        client_sock = server->accept(&peer_addr);
        accepted = (client_sock != nullptr);
    });
    
    auto client = create_socket(SocketType::TCP);
    ASSERT_TRUE(client->connect(server_addr));
    
    accept_thread.join();
    ASSERT_TRUE(accepted);
    ASSERT_NE(client_sock, nullptr);
    
    std::string send_msg = "Hello, Server!";
    EXPECT_EQ(client->send(send_msg.data(), send_msg.size()), 
              static_cast<ssize_t>(send_msg.size()));
    
    char recv_buf[1024] = {0};
    EXPECT_EQ(client_sock->recv(recv_buf, sizeof(recv_buf)), 
              static_cast<ssize_t>(send_msg.size()));
    EXPECT_EQ(std::string(recv_buf, send_msg.size()), send_msg);
}

TEST(SocketTest, TCPSendAndReceiveMultiple) {
    auto server = create_socket(SocketType::TCP);
    NetAddress listen_addr("127.0.0.1", 0);
    
    ASSERT_TRUE(server->bind(listen_addr));
    ASSERT_TRUE(server->listen(5));
    
    auto server_addr = server->local_address();
    
    std::atomic<bool> accepted{false};
    std::unique_ptr<Socket> client_sock;
    
    std::thread accept_thread([&server, &accepted, &client_sock]() {
        NetAddress peer_addr;
        client_sock = server->accept(&peer_addr);
        accepted = (client_sock != nullptr);
    });
    
    auto client = create_socket(SocketType::TCP);
    ASSERT_TRUE(client->connect(server_addr));
    
    accept_thread.join();
    ASSERT_TRUE(accepted);
    ASSERT_NE(client_sock, nullptr);
    
    std::vector<std::string> messages = {"Hello", " ", "World", "!", " Test"};
    std::string expected;
    
    for (const auto& msg : messages) {
        EXPECT_EQ(client->send(msg.data(), msg.size()), 
                  static_cast<ssize_t>(msg.size()));
        expected += msg;
    }
    
    char recv_buf[1024] = {0};
    std::string received;
    while (received.size() < expected.size()) {
        ssize_t n = client_sock->recv(recv_buf, sizeof(recv_buf));
        ASSERT_GT(n, 0);
        received.append(recv_buf, n);
    }
    
    EXPECT_EQ(received, expected);
}

TEST(SocketTest, UDPSendAndReceive) {
    auto server = create_socket(SocketType::UDP);
    NetAddress server_addr("127.0.0.1", 0);
    
    ASSERT_TRUE(server->bind(server_addr));
    auto actual_server_addr = server->local_address();
    
    auto client = create_socket(SocketType::UDP);
    
    std::string send_msg = "Hello, UDP Server!";
    EXPECT_EQ(client->sendto(send_msg.data(), send_msg.size(), actual_server_addr),
              static_cast<ssize_t>(send_msg.size()));
    
    char recv_buf[1024] = {0};
    NetAddress src_addr;
    EXPECT_EQ(server->recvfrom(recv_buf, sizeof(recv_buf), &src_addr),
              static_cast<ssize_t>(send_msg.size()));
    EXPECT_EQ(std::string(recv_buf, send_msg.size()), send_msg);
    EXPECT_FALSE(src_addr.ip().empty());
    EXPECT_GT(src_addr.port(), 0);
}

TEST(SocketTest, UDPBroadcast) {
    auto sock = create_socket(SocketType::UDP);
    
    int broadcast = 1;
    EXPECT_TRUE(sock->set_option(SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)));
    
    NetAddress broadcast_addr("255.255.255.255", 8888);
    std::string msg = "Broadcast message";
    ssize_t sent = sock->sendto(msg.data(), msg.size(), broadcast_addr);
    
    if (sent < 0) {
        std::cout << "广播发送失败: " << sock->error_string() << std::endl;
    }
}

// ============================================================================
// 非阻塞模式测试
// ============================================================================

TEST(SocketTest, NonBlockingMode) {
    auto sock = create_socket(SocketType::TCP);
    
    EXPECT_TRUE(sock->set_nonblocking(true));
    
    NetAddress addr("127.0.0.1", 9999);
    bool connected = sock->connect(addr);
    
    if (!connected) {
        int error = sock->get_error();
        EXPECT_TRUE(error == EINPROGRESS || error == ECONNREFUSED);
    }
    
    EXPECT_TRUE(sock->set_nonblocking(false));
}

TEST(SocketTest, NonBlockingAccept) {
    auto server = create_socket(SocketType::TCP);
    NetAddress addr("127.0.0.1", 0);
    
    ASSERT_TRUE(server->bind(addr));
    ASSERT_TRUE(server->listen(5));
    ASSERT_TRUE(server->set_nonblocking(true));
    
    auto client = server->accept();
    EXPECT_EQ(client, nullptr);
    int error = server->get_error();
    EXPECT_TRUE(error == EAGAIN || error == EWOULDBLOCK);
}

// ============================================================================
// Socket选项测试
// ============================================================================

TEST(SocketTest, SetAndGetOptions) {
    auto sock = create_socket(SocketType::TCP);
    
    SocketOptions options;
    options.reuse_addr = true;
    options.keep_alive = true;
    options.tcp_no_delay = true;
    options.recv_buffer_size = 65536;
    options.send_buffer_size = 65536;
    
    EXPECT_TRUE(sock->set_options(options));
    
    int reuse_addr = 0;
    socklen_t len = sizeof(reuse_addr);
    EXPECT_TRUE(sock->get_option(SOL_SOCKET, SO_REUSEADDR, &reuse_addr, &len));
    EXPECT_EQ(reuse_addr, 1);
    
    int keep_alive = 0;
    len = sizeof(keep_alive);
    EXPECT_TRUE(sock->get_option(SOL_SOCKET, SO_KEEPALIVE, &keep_alive, &len));
    EXPECT_EQ(keep_alive, 1);
    
    int nodelay = 0;
    len = sizeof(nodelay);
    EXPECT_TRUE(sock->get_option(IPPROTO_TCP, TCP_NODELAY, &nodelay, &len));
    EXPECT_EQ(nodelay, 1);
}

TEST(SocketTest, TimeoutOptions) {
    auto sock = create_socket(SocketType::TCP);
    
    SocketOptions options;
    options.recv_timeout_ms = 1000;
    options.send_timeout_ms = 2000;
    
    EXPECT_TRUE(sock->set_options(options));
    
    struct timeval tv;
    socklen_t len = sizeof(tv);
    
    EXPECT_TRUE(sock->get_option(SOL_SOCKET, SO_RCVTIMEO, &tv, &len));
    EXPECT_EQ(tv.tv_sec, 1);
    EXPECT_EQ(tv.tv_usec, 0);
    
    EXPECT_TRUE(sock->get_option(SOL_SOCKET, SO_SNDTIMEO, &tv, &len));
    EXPECT_EQ(tv.tv_sec, 2);
    EXPECT_EQ(tv.tv_usec, 0);
}

// ============================================================================
// 地址信息测试
// ============================================================================

TEST(SocketTest, LocalAddress) {
    auto sock = create_socket(SocketType::TCP);
    NetAddress bind_addr("127.0.0.1", 12347);
    
    ASSERT_TRUE(sock->bind(bind_addr));
    
    auto local_addr = sock->local_address();
    EXPECT_EQ(local_addr.ip(), "127.0.0.1");
    EXPECT_EQ(local_addr.port(), 12347);
}

TEST(SocketTest, PeerAddress) {
    auto server = create_socket(SocketType::TCP);
    NetAddress listen_addr("127.0.0.1", 0);
    
    ASSERT_TRUE(server->bind(listen_addr));
    ASSERT_TRUE(server->listen(5));
    
    auto server_addr = server->local_address();
    
    std::atomic<bool> accepted{false};
    std::unique_ptr<Socket> client_sock;
    NetAddress peer_addr;
    
    std::thread accept_thread([&server, &accepted, &client_sock, &peer_addr]() {
        client_sock = server->accept(&peer_addr);
        accepted = (client_sock != nullptr);
    });
    
    auto client = create_socket(SocketType::TCP);
    ASSERT_TRUE(client->connect(server_addr));
    
    accept_thread.join();
    ASSERT_TRUE(accepted);
    
    EXPECT_FALSE(peer_addr.ip().empty());
    EXPECT_GT(peer_addr.port(), 0);
    
    auto client_peer = client->peer_address();
    EXPECT_EQ(client_peer.ip(), server_addr.ip());
    EXPECT_EQ(client_peer.port(), server_addr.port());
}

// ============================================================================
// 错误处理测试
// ============================================================================

TEST(SocketTest, ErrorHandling) {
    auto sock = create_socket(SocketType::TCP);
    
    EXPECT_EQ(sock->get_error(), 0);
    
    NetAddress invalid_addr("999.999.999.999", 80);
    EXPECT_FALSE(sock->bind(invalid_addr));
    
    EXPECT_NE(sock->get_error(), 0);
    EXPECT_FALSE(sock->error_string().empty());
}

TEST(SocketTest, OperationsOnClosedSocket) {
    auto sock = create_socket(SocketType::TCP);
    sock->close();
    
    EXPECT_FALSE(sock->is_valid());
    
    NetAddress addr("127.0.0.1", 80);
    EXPECT_FALSE(sock->bind(addr));
    EXPECT_FALSE(sock->listen(5));
    EXPECT_EQ(sock->send("test", 4), -1);
    EXPECT_EQ(sock->recv(nullptr, 0), -1);
}

// ============================================================================
// 关闭和资源管理测试
// ============================================================================

TEST(SocketTest, CloseSocket) {
    auto sock = create_socket(SocketType::TCP);
    int fd = sock->fd();
    EXPECT_GE(fd, 0);
    
    sock->close();
    EXPECT_FALSE(sock->is_valid());
    EXPECT_EQ(sock->fd(), -1);
}

TEST(SocketTest, RAIIResourceManagement) {
    int fd = -1;
    {
        auto sock = create_socket(SocketType::TCP);
        fd = sock->fd();
        EXPECT_GE(fd, 0);
    }
    
    char buf[1];
    EXPECT_LT(::read(fd, buf, 1), 0);
    EXPECT_EQ(errno, EBADF);
}

TEST(SocketTest, MoveSemantics) {
    auto sock1 = create_socket(SocketType::TCP);
    int fd1 = sock1->fd();
    
    // 移动构造
    auto sock2 = std::move(sock1);
    EXPECT_EQ(sock1.get(), nullptr);
    EXPECT_TRUE(sock2->is_valid());
    EXPECT_EQ(sock2->fd(), fd1);
    
    // 移动赋值
    auto sock3 = create_socket(SocketType::TCP);
    sock3 = std::move(sock2);
    
    EXPECT_TRUE(sock3->is_valid());
    EXPECT_EQ(sock3->fd(), fd1);
    EXPECT_EQ(sock2.get(), nullptr);
}

TEST(SocketTest, UnixStreamSocket) {
    GTEST_SKIP() << "Unix 域套接字暂不支持";
}

TEST(SocketTest, DISABLED_Throughput) {
    const size_t total_size = 100 * 1024 * 1024;
    const size_t buffer_size = 64 * 1024;
    std::string data(buffer_size, 'A');
    
    auto server = create_socket(SocketType::TCP);
    NetAddress listen_addr("127.0.0.1", 0);
    
    ASSERT_TRUE(server->bind(listen_addr));
    ASSERT_TRUE(server->listen(5));
    auto server_addr = server->local_address();
    
    std::atomic<bool> accepted{false};
    std::unique_ptr<Socket> client_sock;
    
    std::thread accept_thread([&server, &accepted, &client_sock]() {
        client_sock = server->accept();
        accepted = (client_sock != nullptr);
    });
    
    auto client = create_socket(SocketType::TCP);
    ASSERT_TRUE(client->connect(server_addr));
    
    accept_thread.join();
    ASSERT_TRUE(accepted);
    
    auto start = std::chrono::steady_clock::now();
    
    size_t sent_total = 0;
    while (sent_total < total_size) {
        size_t to_send = std::min(buffer_size, total_size - sent_total);
        ssize_t n = client->send(data.data(), to_send);
        ASSERT_GT(n, 0);
        sent_total += n;
    }
    
    auto end = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    double throughput = (total_size / 1024.0 / 1024.0) / (ms / 1000.0);
    std::cout << "发送吞吐量: " << throughput << " MB/s" << std::endl;
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
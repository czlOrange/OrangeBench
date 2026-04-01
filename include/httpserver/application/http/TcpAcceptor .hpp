// ========== 1. TcpAcceptor - TCP 连接管理 ==========
// 就像大门保安：迎接客人，安排座位

class TcpAcceptor {
public:
    TcpAcceptor(std::shared_ptr<core::IIOHandler> io_handler,
                std::shared_ptr<core::IEventDispatcher> dispatcher);
    
    // 开门营业
    bool Listen(const std::string& host, uint16_t port);
    
    // 设置新连接回调
    void SetOnConnection(std::function<void(std::shared_ptr<core::IConnection>)> callback);
    
private:
    void acceptLoop();
    std::shared_ptr<core::IIOHandler> io_handler_;
    std::shared_ptr<core::IEventDispatcher> dispatcher_;
    int listen_fd_;
    std::atomic<bool> running_;
    std::function<void(std::shared_ptr<core::IConnection>)> on_connection_;
};
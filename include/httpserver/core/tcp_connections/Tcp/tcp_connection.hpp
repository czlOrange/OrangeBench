// src/httpserver/core/connections/tcp_connection.hpp
#pragma once

#include "./Connection/connection_standard.hpp"
//#include "connection_manager.hpp"
#include "./IO/io_handler.hpp"
#include "./Buffer/buffer_manager.hpp"
#include "./Async/async_operation.hpp"
#include "./Event/event_dispatcher.hpp"
#include <atomic>
#include <mutex>

namespace httpserver::core {

// TCP连接实现类
// 就像一位快递员，负责一条具体的送货线路，管理从建立联系到完成配送的全过程
class TCPConnection : public IConnection {
public:
    // 构造函数：招募一位新快递员，配备好各种工具（对讲机、包裹袋、调度系统、事件通知器）
    TCPConnection(
        std::shared_ptr<IIOHandler> io_handler,           // 对讲机（负责实际通话）
        std::shared_ptr<IBufferManager> buffer_manager,   // 包裹袋（临时存放货物）
        std::shared_ptr<IAsyncScheduler> scheduler,       // 调度系统（安排送货时间）
        std::shared_ptr<IEventDispatcher> dispatcher);    // 事件通知器（重要事情喊一嗓）
    
    // 特殊情况：(从现有文件描述符创建)接手一个已经在路上的快递单（比如服务器accept接收到的连接）
    TCPConnection(
        int existing_fd,                                   // 已有的对讲机频道
        const SocketAddress& peer_addr,                    // 对方地址
        std::shared_ptr<IIOHandler> io_handler,
        std::shared_ptr<IBufferManager> buffer_manager,
        std::shared_ptr<IAsyncScheduler> scheduler,
        std::shared_ptr<IEventDispatcher> dispatcher);
    
    // 快递员离职，归还所有装备
    ~TCPConnection() override;
    
    // 快递员不能复制（每个快递员都是独一无二的）
    TCPConnection(const TCPConnection&) = delete;
    TCPConnection& operator=(const TCPConnection&) = delete;
    
    // ────────────────────────────────────────────
    // 连接生命周期管理（快递员的日常工作）
    // ────────────────────────────────────────────
    
    // 拨通对方电话，建立联系（根据对方地址）
    std::error_code Connect(const std::string& host, uint16_t port) override;
    std::error_code Connect(const SocketAddress& addr) override;
    
    // 挂断电话，结束本次通话（但保留通话设备）
    std::error_code Disconnect() noexcept override;
    
    // 彻底销毁通话设备，结束一切
    void Close() noexcept override;
    
    // ────────────────────────────────────────────
    // 数据收发（送货和收货）
    // ────────────────────────────────────────────
    
    // 送货（同步方式）：当场把货送出去，等送完才回来
    std::expected<size_t, std::error_code> Send(std::string_view data) override;
    std::expected<size_t, std::error_code> Send(const void* data, size_t len) override;
    
    // 收货（同步方式）：站在那等货到，拿到才走
    std::expected<std::string, std::error_code> Receive(size_t max_len) override;
    std::expected<size_t, std::error_code> Receive(void* buffer, size_t len) override;
    
    // 送货（异步方式）：把货交给调度系统，拿到回执就去做别的事
    AsyncResult<size_t> SendAsync(std::string_view data) override;
    AsyncResult<size_t> SendAsync(const void* data, size_t len) override;
    
    // 收货（异步方式）：告诉调度系统帮我收货，货到了通知我
    AsyncResult<std::string> ReceiveAsync(size_t max_len) override;
    AsyncResult<size_t> ReceiveAsync(void* buffer, size_t len) override;
    
    // ────────────────────────────────────────────
    // 缓冲区管理（包裹袋管理）
    // ────────────────────────────────────────────
    
    // 把袋子里的货都发出去（清空待发送包裹）
    std::error_code Flush() override;
    
    // 清空所有包裹袋（丢弃所有待发送/已接收数据）
    void ClearBuffers() noexcept override;
    
    // 看看袋子里还有多少货待发送
    size_t GetPendingSendBytes() const noexcept override;
    
    // 看看已经收了但还没处理的货有多少
    size_t GetPendingReceiveBytes() const noexcept override;
    
    // ────────────────────────────────────────────
    // 配置管理（调整工作方式）
    // ────────────────────────────────────────────
    
    // 设置快递员的工作规则（超时时间、重试次数等）
    void Configure(const ConnectionConfig& config) override;
    
    // 查看当前工作规则
    ConnectionConfig GetConfig() const override;
    
    // 更新部分工作规则
    void UpdateConfig(std::function<void(ConnectionConfig&)> updater) override;
    
    // ────────────────────────────────────────────
    // 状态查询（快递员当前状态）
    // ────────────────────────────────────────────
    
    // 当前处于什么状态（待命、通话中、已挂断、异常）
    ConnectionState GetState() const override;
    
    // 是否正在通话中
    bool IsConnected() const override;
    
    // 是否可以接收数据（对方是否在说话）
    bool IsReadable() const override;
    
    // 是否可以发送数据（线路是否畅通）
    bool IsWritable() const override;
    
    // 是否有异常情况
    bool HasError() const override;
    
    // ────────────────────────────────────────────
    // 地址信息（对方是谁，我是谁）
    // ────────────────────────────────────────────
    
    // 我的电话号码（本地地址）
    std::string GetLocalAddress() const override;
    
    // 对方的电话号码（对端地址）
    std::string GetPeerAddress() const override;
    
    // 我的分机号（本地端口）
    uint16_t GetLocalPort() const noexcept override;
    
    // 对方的分机号（对端端口）
    uint16_t GetPeerPort() const noexcept override;
    
    // 我的详细地址（完整SocketAddress）
    SocketAddress GetLocalSocketAddress() const override;
    
    // 对方的详细地址
    SocketAddress GetPeerSocketAddress() const override;
    
    // ────────────────────────────────────────────
    // 统计信息（工作记录）
    // ────────────────────────────────────────────
    
    // 总共送了多少货（发送字节数）
    size_t GetBytesSent() const noexcept override;
    
    // 总共收了多少货（接收字节数）
    size_t GetBytesReceived() const noexcept override;
    
    // 总共处理了多少次操作
    size_t GetTotalOperations() const noexcept override;
    
    // 什么时候开始这次通话的
    std::chrono::steady_clock::time_point GetConnectTime() const override;
    
    // 最后一次活动是什么时候
    std::chrono::steady_clock::time_point GetLastActivityTime() const override;
    
    // ────────────────────────────────────────────
    // 超时等待（等货/等线路）
    // ────────────────────────────────────────────
    
    // 等货到，最多等多久
    bool WaitForData(std::chrono::milliseconds timeout) override;
    
    // 等线路空出来可以发货，最多等多久
    bool WaitForWritable(std::chrono::milliseconds timeout) override;
    
    // ────────────────────────────────────────────
    // 文件描述符（对讲机频道号）
    // ────────────────────────────────────────────
    
    // 获取当前使用的对讲机频道号
    int GetFd() const override;
    
    // ────────────────────────────────────────────
    // 事件回调（有事打我电话）
    // ────────────────────────────────────────────
    
    // 设置事件回调（线路故障、挂断等）
    void SetEventCallback(EventCallback callback) override;
    
    // 设置数据回调（收到货了喊我）
    void SetDataCallback(DataCallback callback) override;
    
    // 设置错误回调（出错了喊我）
    void SetErrorCallback(ErrorCallback callback) override;
    
    // ────────────────────────────────────────────
    // 连接ID（快递员工号）
    // ────────────────────────────────────────────
    
    // 获取工号
    uint64_t GetConnectionId() const noexcept override;
    
    // 设置工号
    void SetConnectionId(uint64_t id) override;
    
    // ────────────────────────────────────────────
    // 管理器关联（归哪个站点管理）
    // ────────────────────────────────────────────
    
    // 设置所属的快递站点（连接管理器）
    void SetConnectionManager(std::shared_ptr<IConnectionManager> manager) override;
    
    // 获取所属的快递站点
    std::shared_ptr<IConnectionManager> GetConnectionManager() const override;
    
    // ────────────────────────────────────────────
    // 自定义数据（随身携带的小本本）
    // ────────────────────────────────────────────
    
    // 在小本本上记点东西
    void SetUserData(const std::string& key, std::any data) override;
    
    // 从小本本上查看记的东西
    std::any GetUserData(const std::string& key) const override;
    
    // 看看小本本上有没有记这个
    bool HasUserData(const std::string& key) const override;
    
    // 把小本本上的记录划掉
    void RemoveUserData(const std::string& key) override;
    
    // ────────────────────────────────────────────
    // 心跳（定时报平安）
    // ────────────────────────────────────────────
    
    // 更新心跳时间（证明还活着）
    void UpdateHeartbeat() override;
    
    // 检查是否太久没报平安了
    bool IsHeartbeatExpired(std::chrono::milliseconds timeout) const override;
    
private:
    // 依赖组件（快递员的各种工具）
    std::shared_ptr<IIOHandler> io_handler_;           // 对讲机
    std::shared_ptr<IBufferManager> buffer_manager_;   // 总包裹仓库
    std::shared_ptr<ConnectionBufferManager> connection_buffer_; // 随身包裹袋
    std::shared_ptr<IAsyncScheduler> scheduler_;       // 调度系统
    std::shared_ptr<IEventDispatcher> dispatcher_;     // 事件通知器
    std::shared_ptr<ConnectionEventManager> event_manager_; // 事件管理器
    std::shared_ptr<IConnectionManager> connection_manager_; // 所属站点
    
    // 连接状态（当前工作状态）
    std::atomic<ConnectionState> state_{ConnectionState::DISCONNECTED}; // 在忙吗？
    ConnectionConfig config_;                           // 工作规则
    std::mutex state_mutex_;                            // 状态变更的锁
    
    // Socket信息（通话设备信息）
    SocketFd socket_fd_{-1};                             // 对讲机频道号
    SocketAddress local_addr_;                           // 我的地址
    SocketAddress peer_addr_;                            // 对方地址
    
    // 统计信息（工作记录本）
    std::atomic<size_t> bytes_sent_{0};                  // 累计发货量
    std::atomic<size_t> bytes_received_{0};              // 累计收货量
    std::atomic<size_t> total_operations_{0};            // 累计操作次数
    std::chrono::steady_clock::time_point connect_time_; // 本次上班时间
    std::chrono::steady_clock::time_point last_activity_time_; // 最后一次干活时间
    std::chrono::steady_clock::time_point last_heartbeat_time_; // 最后一次报平安时间
    
    // 连接标识
    uint64_t connection_id_{0};                          // 工号
    
    // 自定义数据存储（随身小本本）
    std::unordered_map<std::string, std::any> user_data_;
    mutable std::mutex user_data_mutex_;
    
    // 私有方法（内部工作流程）
    bool setup_socket();                                  // 初始化对讲机
    std::error_code handle_socket_error();                // 处理设备故障
    void update_activity_timestamp();                     // 更新干活时间
    
    // SSL支持（加密通话，条件编译）
#ifdef HTTPSERVER_SSL_SUPPORT
    class SSLContext;                                      // 加密器
    std::unique_ptr<SSLContext> ssl_context_;             // 加密设备
    bool init_ssl();                                       // 开启加密
    void cleanup_ssl();                                    // 关闭加密
#endif
};

} // namespace httpserver::core
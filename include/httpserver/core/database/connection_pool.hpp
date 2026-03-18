// include/httpserver/database/connection_pool.hpp
#pragma once

#include "../coroutine/scheduler.hpp"
#include <memory>
#include <string>
#include <chrono>

namespace httpserver::database {

// 数据库连接配置
struct DatabaseConfig {
    std::string host;
    uint16_t port;
    std::string username;
    std::string password;
    std::string database;
    std::string charset = "utf8mb4";
    
    // 连接池配置
    size_t min_connections = 5;
    size_t max_connections = 100;
    size_t max_idle_time = 300;  // 秒
    
    // 连接超时
    std::chrono::milliseconds connect_timeout{5000};
    std::chrono::milliseconds query_timeout{30000};
    
    // 重试策略
    size_t max_retry_attempts = 3;
    std::chrono::milliseconds retry_delay{1000};
    
    // 监控
    bool enable_monitoring = true;
    size_t stats_interval_ms = 5000;
};

// 查询结果
class QueryResult {
public:
    virtual ~QueryResult() = default;
    
    virtual bool HasNext() const = 0;
    virtual bool Next() = 0;
    
    // 获取字段值
    virtual std::string GetString(size_t column) const = 0;
    virtual int GetInt(size_t column) const = 0;
    virtual int64_t GetInt64(size_t column) const = 0;
    virtual double GetDouble(size_t column) const = 0;
    virtual bool GetBool(size_t column) const = 0;
    
    // 获取NULL状态
    virtual bool IsNull(size_t column) const = 0;
    
    // 获取列信息
    virtual size_t GetColumnCount() const = 0;
    virtual std::string GetColumnName(size_t column) const = 0;
    
    // 转换为JSON
    virtual std::string ToJson() const = 0;
};

// 预处理语句
class PreparedStatement {
public:
    virtual ~PreparedStatement() = default;
    
    // 绑定参数
    virtual void BindInt(size_t index, int value) = 0;
    virtual void BindInt64(size_t index, int64_t value) = 0;
    virtual void BindDouble(size_t index, double value) = 0;
    virtual void BindString(size_t index, std::string_view value) = 0;
    virtual void BindNull(size_t index) = 0;
    virtual void BindBool(size_t index, bool value) = 0;
    
    // 执行
    virtual std::unique_ptr<QueryResult> Execute() = 0;
};

// 数据库连接接口
class IDatabaseConnection : public core::Connection {
public:
    virtual ~IDatabaseConnection() = default;
    
    // 数据库特定操作
    virtual bool Ping() = 0;
    virtual std::unique_ptr<QueryResult> ExecuteQuery(std::string_view sql) = 0;
    virtual int ExecuteUpdate(std::string_view sql) = 0;
    virtual int64_t ExecuteInsert(std::string_view sql) = 0;
    
    // 事务支持
    virtual bool BeginTransaction() = 0;
    virtual bool Commit() = 0;
    virtual bool Rollback() = 0;
    
    // 预处理语句
    virtual std::unique_ptr<PreparedStatement> PrepareStatement(std::string_view sql) = 0;
    
    // 批量操作
    virtual bool ExecuteBatch(const std::vector<std::string>& queries) = 0;
    
    // 元数据
    virtual std::vector<std::string> GetTables() = 0;
    virtual std::vector<std::string> GetColumns(std::string_view table) = 0;
    
    // 连接信息
    virtual std::string GetServerVersion() const = 0;
    virtual bool IsReadOnly() const = 0;
};

// 协程化的数据库连接
class CoroutineDatabaseConnection {
public:
    explicit CoroutineDatabaseConnection(std::shared_ptr<IDatabaseConnection> conn);
    
    // 协程化查询
    coroutine::Task<std::unique_ptr<QueryResult>> Query(std::string_view sql);
    coroutine::Task<int> Update(std::string_view sql);
    coroutine::Task<int64_t> Insert(std::string_view sql);
    
    // 事务（协程感知）
    class Transaction {
    public:
        explicit Transaction(std::shared_ptr<IDatabaseConnection> conn);
        
        template<typename Func>
        coroutine::Task<bool> Execute(Func func);
        
        bool Commit();
        bool Rollback();
        
    private:
        std::shared_ptr<IDatabaseConnection> connection_;
        bool committed_ = false;
    };
    
    coroutine::Task<bool> Transactional(std::function<coroutine::Task<bool>(Transaction&)> operation);
    
    // 流式查询结果
    class QueryStream {
    public:
        explicit QueryStream(std::unique_ptr<QueryResult> result);
        
        coroutine::Task<std::optional<std::vector<std::string>>> FetchRow();
        coroutine::Task<std::vector<std::vector<std::string>>> FetchAll();
        
        template<typename T>
        coroutine::Task<std::optional<T>> FetchOne(std::function<T(const QueryResult&)> mapper);
        
        template<typename T>
        coroutine::Task<std::vector<T>> FetchAll(std::function<T(const QueryResult&)> mapper);
    };
    
    QueryStream StreamQuery(std::string_view sql);
};

// 数据库连接池接口
class IDatabaseConnectionPool {
public:
    virtual ~IDatabaseConnectionPool() = default;
    
    // 配置管理
    virtual bool Initialize(const DatabaseConfig& config) = 0;
    virtual void Shutdown() = 0;
    
    // 连接获取（阻塞）
    virtual std::shared_ptr<IDatabaseConnection> GetConnection() = 0;
    virtual void ReleaseConnection(std::shared_ptr<IDatabaseConnection> conn) = 0;
    
    // 协程化连接获取
    virtual coroutine::Task<std::shared_ptr<CoroutineDatabaseConnection>> 
        GetCoroutineConnection() = 0;
    
    virtual coroutine::Task<> 
        ReleaseCoroutineConnection(std::shared_ptr<CoroutineDatabaseConnection> conn) = 0;
    
    // 连接池管理
    virtual void SetMinConnections(size_t min) = 0;
    virtual void SetMaxConnections(size_t max) = 0;
    virtual void SetMaxIdleTime(std::chrono::seconds time) = 0;
    
    // 监控和统计
    struct PoolStats {
        size_t total_connections;
        size_t active_connections;
        size_t idle_connections;
        size_t waiting_requests;
        
        std::chrono::milliseconds avg_wait_time;
        std::chrono::milliseconds max_wait_time;
        
        size_t connection_creates;
        size_t connection_destroys;
        size_t connection_timeouts;
    };
    
    virtual PoolStats GetStats() const = 0;
    virtual bool HealthCheck() = 0;
    
    // 自动重连
    virtual void EnableAutoReconnect(bool enable) = 0;
    virtual bool IsAutoReconnectEnabled() const = 0;
    
    // 负载均衡支持（多主/从）
    class LoadBalancedPool {
    public:
        virtual ~LoadBalancedPool() = default;
        
        // 读写分离
        virtual coroutine::Task<std::shared_ptr<CoroutineDatabaseConnection>> 
            GetReadConnection() = 0;
        
        virtual coroutine::Task<std::shared_ptr<CoroutineDatabaseConnection>> 
            GetWriteConnection() = 0;
        
        // 添加/移除节点
        virtual void AddNode(const DatabaseConfig& config) = 0;
        virtual void RemoveNode(std::string_view host) = 0;
        
        // 节点健康检查
        virtual std::vector<bool> CheckNodes() = 0;
    };
    
    // 创建负载均衡池
    virtual std::unique_ptr<LoadBalancedPool> 
        CreateLoadBalancedPool(const std::vector<DatabaseConfig>& nodes) = 0;
};

} // namespace httpserver::database
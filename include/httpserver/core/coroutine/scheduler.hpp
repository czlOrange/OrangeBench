// include/httpserver/coroutine/scheduler.hpp
#pragma once

#include <coroutine>
#include <functional>
#include <memory>
#include <chrono>
#include <expected>
#include <future>

namespace httpserver::coroutine {

// 协程任务返回类型
template<typename T = void>     //模板类默认是void类型
class Task;                     //前向声明




// 协程句柄包装
class CoroutineHandle {
public:
    virtual ~CoroutineHandle() = default;
    
    // 恢复协程执行
    virtual bool resume() = 0;
    
    // 检查协程是否完成
    virtual bool done() const = 0;
    
    // 获取协程ID
    virtual size_t id() const = 0;
    
    // 销毁协程
    virtual void destroy() = 0;
};



// 协程等待器接口
class IAwaiter {
public:
    virtual ~IAwaiter() = default;                                  // 虚析构函数
    virtual bool await_ready() const noexcept = 0;                  // 是否准备好继续执行        
    virtual void await_suspend(std::coroutine_handle<> handle) = 0; // 挂起协程        
    virtual void await_resume() = 0;                                // 恢复挂起后马上需要执行的逻辑
};

// 定时等待器
class TimerAwaiter : public IAwaiter {
public:
    explicit TimerAwaiter(std::chrono::milliseconds duration);

    // 实现 IAwaiter 接口
    bool await_ready() override;
    void await_suspend(std::coroutine_handle<> handle) override;
    void await_resume() override;

private:
    // 关键：补全成员变量
    std::chrono::milliseconds duration_;                // 存储延时时长
    std::chrono::steady_clock::time_point start_time_;  // 存储启动时间
};

// 网络等待器（用于连接IO）
// 网络等待器类模板（补全核心声明）
template<typename Result>
class NetworkAwaiter : public IAwaiter {
public:
    using Callback = std::function<void(Result)>;
    
    // 构造函数声明
    explicit NetworkAwaiter(std::function<void(Callback)> async_operation);

    // 必须声明的协程等待器接口（继承自IAwaiter）
    bool await_ready() override;
    void await_suspend(std::coroutine_handle<> handle) override;
    Result await_resume() override;

private:
    // 核心成员变量（存储异步操作、结果、协程句柄等）
    std::function<void(Callback)> async_op_; // 保存传入的异步网络操作
    Result result_;                          // 存储异步操作的结果
    std::coroutine_handle<> coro_handle_;    // 存储挂起的协程句柄
    bool is_completed_ = false;              // 标记异步操作是否完成
};




// 协程调度器配置结构体：集中管理有栈协程调度器的核心运行参数
struct SchedulerConfig {
    size_t worker_threads = std::thread::hardware_concurrency(); // 工作线程数，默认等于CPU硬件核心数（最大化多核利用率）
    size_t max_coroutines = 1000000;  // 百万协程：调度器支持的最大并发协程数量，默认百万级（适配高并发场景）
    size_t stack_size = 64 * 1024;    // 64KB栈大小：单个有栈协程的栈内存大小，平衡内存占用与栈需求
    bool enable_work_stealing = true; // 启用工作窃取：空闲线程可从繁忙线程偷取协程执行，提升整体调度效率
    bool enable_priority = false;     // 关闭优先级调度：默认按FIFO调度，降低调度开销（开启可支持协程优先级）
    std::chrono::milliseconds idle_timeout{5000}; // 线程空闲超时时间：空闲5秒无任务则休眠/退出，减少资源占用
    
    // 统计和监控相关配置
    bool enable_stats = true;         // 启用统计监控：收集调度器运行指标（协程数、线程负载等）
    size_t stats_interval_ms = 1000;  // 统计上报间隔：每1000毫秒（1秒）输出一次统计数据
};

// 协程调度器统计结构体：记录有栈协程调度器的运行状态和性能指标
struct SchedulerStats {
    std::atomic<uint64_t> total_coroutines{0};       // 累计创建的协程总数（原子类型保证多线程安全）
    std::atomic<uint64_t> active_coroutines{0};      // 当前活跃的协程数（运行/挂起未完成的协程）
    std::atomic<uint64_t> completed_coroutines{0};   // 已正常完成的协程总数
    std::atomic<uint64_t> failed_coroutines{0};      // 执行失败/异常退出的协程总数
    std::atomic<uint64_t> context_switches{0};       // 协程上下文切换总次数（调度器核心性能指标）
    
    // 性能统计
    std::atomic<uint64_t> avg_wait_time_ns{0};       // 协程平均等待时间（单位：纳秒，从入队到执行的耗时）
    std::atomic<uint64_t> max_wait_time_ns{0};       // 协程最大等待时间（单位：纳秒，反映调度器峰值延迟）
    std::atomic<uint64_t> throughput_per_sec{0};     // 每秒完成协程数：调度器处理能力（吞吐量）
};

// 协程调度器接口
class IScheduler {
public:
    virtual ~IScheduler() = default;
    
    // 初始化与关闭
    virtual bool Initialize(const SchedulerConfig& config) = 0;
    virtual void Shutdown() noexcept = 0;
    
    // 协程调度
    template<typename Func>
    Task<> Schedule(Func func);
    
    template<typename Func, typename... Args>
    Task<> Schedule(Func func, Args&&... args);
    
    // 批量调度
    template<typename Func>
    std::vector<Task<>> ScheduleBatch(size_t count, Func func);
    
    // 定时调度
    template<typename Func>
    Task<> ScheduleAfter(std::chrono::milliseconds delay, Func func);
    
    template<typename Func>
    Task<> ScheduleAt(std::chrono::steady_clock::time_point time, Func func);
    
    // 协程同步原语
    virtual Task<> Yield() = 0;
    virtual Task<> Sleep(std::chrono::milliseconds duration) = 0;
    
    // 等待多个任务完成
    virtual Task<> WhenAll(std::vector<Task<>> tasks) = 0;
    template<typename T>
    virtual Task<std::vector<T>> WhenAll(std::vector<Task<T>> tasks) = 0;
    
    virtual Task<> WhenAny(std::vector<Task<>> tasks) = 0;
    
    // IO操作集成
    template<typename T>
    Task<T> AsyncIO(std::function<void(std::function<void(std::expected<T, std::error_code>)>)> io_operation);
    
    // 连接协程适配器（核心：将连接操作转换为协程）
    template<typename ConnectionType>
    class ConnectionAdapter;
    
    // 监控和管理
    virtual SchedulerStats GetStats() const = 0;
    virtual size_t GetPendingTasks() const = 0;
    virtual size_t GetRunningTasks() const = 0;
    
    virtual void SetMaxConcurrency(size_t max) = 0;
    virtual size_t GetMaxConcurrency() const = 0;
    
    // 错误处理
    virtual void SetUnhandledExceptionHandler(
        std::function<void(std::exception_ptr)> handler) = 0;
    
    // 调试支持
    virtual void EnableDebug(bool enable) = 0;
    virtual std::vector<size_t> GetActiveCoroutineIds() const = 0;
    
protected:
    // 内部调度方法
    virtual void EnqueueCoroutine(std::coroutine_handle<> handle, int priority = 0) = 0;
    virtual void DequeueCoroutine(std::coroutine_handle<> handle) = 0;
};






// 任务类定义
template<typename T>
class Task {
public:
    struct promise_type {
        Task<T> get_return_object();                    //对外提供回执,返回任务对象
        std::suspend_always initial_suspend();          //挂点在初始后,协程开始时挂起,等待调度器调度 
        std::suspend_always final_suspend() noexcept;   //挂点在末尾前,协程结束时挂起,等待调度器清理
        void return_value(T value);                     //负责把co_return的值存入value中
        void unhandled_exception();                     //负责把std::current_exception()信息存入exception中        
                 
        std::shared_ptr<T> value;                       //存储返回值的容器          
        std::exception_ptr exception;                   //存储异常信息的容器
        std::coroutine_handle<> continuation;           //存储继续执行的协程句柄
    };
    
    // 可以移动,不能拷贝
    Task() = default;                       //默认构造函数
    Task(Task&& other) noexcept;            //移动构造函数
    Task& operator=(Task&& other) noexcept; //移动赋值运算符
    ~Task();                                //析构函数
    
    // 回执操作
    bool IsReady() const;       // 检查任务是否完成,"小王办完事了吗？"  
    T Wait();                   // 阻塞等待任务完成,"小王把事办完了告诉我"        
    std::future<T> AsFuture();  // 异步等待任务结果,"我先去忙别的事，等会儿再来问小王"   
    bool Cancel();              // 直接直接取消任务,"小王别办了，事情取消了"
    std::expected<T, std::error_code> TryGet();// 获取结果（如果已完成）
    
private:
    std::coroutine_handle<promise_type> handle_;
};

// 专门用于void的特化
template<>
class Task<void> {
public:
    struct promise_type {
        Task<void> get_return_object();
        std::suspend_always initial_suspend();
        std::suspend_always final_suspend() noexcept;
        void return_void();
        void unhandled_exception();
        
        std::coroutine_handle<> continuation;
        std::exception_ptr exception;
    };
    
    // ... 类似的方法，但没有返回值
};







} // namespace httpserver::coroutine
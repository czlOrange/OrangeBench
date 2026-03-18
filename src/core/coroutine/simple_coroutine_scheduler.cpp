#include <coroutine>
#include <iostream>
#include <utility>
#include <string>
#include <vector>

struct SimpleTask {
struct promise_type {        
    // 必实现1：协程创建时【编译器第一个自动调用】，返回当前协程的返回对象(SimpleTask)
    // 作用：把「协程句柄」绑定到「SimpleTask对象」上，让外部能通过SimpleTask操控协程
    SimpleTask get_return_object() noexcept {
        return SimpleTask{std::coroutine_handle<promise_type>::from_promise(*this)};
    }

    // 必实现2：协程创建完成后，【准备开始执行前】编译器自动调用
    // 作用：决定协程的「启动策略」；返回suspend_never = 协程创建后【立即自动执行】
    std::suspend_never initial_suspend() noexcept { return {}; }

    // 必实现3：协程函数体内所有代码【执行完毕后】编译器自动调用
    // 作用：决定协程的「销毁策略」；返回suspend_never = 协程执行完【立即自动销毁】释放资源
    std::suspend_never final_suspend() noexcept { return {}; }

    // 必实现4：处理协程函数的「返回逻辑」
    // 作用：你的协程函数是无返回值的(void类型逻辑)，这个空实现刚好匹配，处理 co_return; 或 无返回语句的场景
    void return_void() noexcept {}

    // 必实现5：处理协程函数体内【未捕获的异常】
    // 作用：协程内抛异常且没try/catch时，编译器自动调用这个函数，这里写terminate()直接终止程序，是标准兜底逻辑
    void unhandled_exception() noexcept { std::terminate(); }
};

    std::coroutine_handle<promise_type> coro_handle;

    explicit SimpleTask(std::coroutine_handle<promise_type> h) noexcept : coro_handle(h) {}
    SimpleTask(const SimpleTask&) = delete;
    SimpleTask& operator=(const SimpleTask&) = delete;
    SimpleTask(SimpleTask&& other) noexcept : coro_handle(std::exchange(other.coro_handle, {})) {}
    SimpleTask& operator=(SimpleTask&& other) noexcept {
        if (this != &other) {
            coro_handle = std::exchange(other.coro_handle, {});
        }
        return *this;
    }
    ~SimpleTask() noexcept {
        if (coro_handle) coro_handle.destroy();
    }
};

struct Yield {
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<>) const noexcept {}
    void await_resume() const noexcept {}  
};

// 复用方式1：不同业务的协程函数
SimpleTask task_print_log() {
    std::cout << "[日志协程] 第一步：日志初始化\n";
    co_await Yield{};
    std::cout << "[日志协程] 第二步：日志写入完成\n";
    co_await Yield{};
    std::cout << "[日志协程] 执行完毕\n";
}

// 复用方式2：通用模板协程函数（批量创建核心）
SimpleTask task_template(int task_id) {
    std::cout << "[协程-" << task_id << "] 第一步：业务启动\n";
    co_await Yield{};
    std::cout << "[协程-" << task_id << "] 第二步：业务处理完成\n";
    co_await Yield{};
    std::cout << "[协程-" << task_id << "] 执行完毕\n";
}

int main() {
    // 方式1：调用独立业务协程
    std::cout << "===== 独立业务协程 =====\n";
    auto log_task = task_print_log();
    log_task.coro_handle.resume();
    log_task.coro_handle.resume();

    // 方式2：批量创建5个协程实例
    std::cout << "\n===== 批量创建协程 =====\n";
    std::vector<SimpleTask> coros;
    coros.reserve(5);
    for(int i=1; i<=5; ++i) coros.emplace_back(task_template(i));
    for(int i=0; i<5; ++i) {
        coros[i].coro_handle.resume();
        coros[i].coro_handle.resume();
    }

    return 0;
}
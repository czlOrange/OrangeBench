// include/httpserver/core/async/async_operation_impl.hpp
#pragma once

#include "async_operation.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <thread>
#include <stdexcept>

namespace httpserver::core::async {

/**
 * @brief 基于 std::promise/future 的异步操作实现（改进版）
 */
template<typename T>
class PromiseOperation : public ITypedOperation<T> {
public:
    PromiseOperation() 
        : state_(State::PENDING)
        , cancelled_(false)
        , result_set_(false)
        , completed_callback_(nullptr)
        , cancelled_callback_(nullptr)
        , stored_exception_(nullptr) {
        future_ = promise_.get_future();
        wait_thread_ = std::thread([this] { waitLoop(); });
    }

    ~PromiseOperation() override {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (state_ == State::PENDING) {
                state_ = State::CANCELLED;
                cancelled_ = true;
                result_set_ = true;
            }
            cv_.notify_one();
        }
        if (wait_thread_.joinable()) {
            wait_thread_.join();
        }
    }

    void SetResult(T value) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (result_set_) return;
        try {
            promise_.set_value(std::move(value));
        } catch (const std::future_error&) {
            return;
        }
        result_set_ = true;
        state_ = State::COMPLETED;
        cv_.notify_one();
        if (completed_callback_) completed_callback_();
    }

    void SetException(std::exception_ptr e) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (result_set_) return;
        try {
            promise_.set_exception(e);
        } catch (const std::future_error&) {
            return;
        }
        stored_exception_ = e;  // 保存异常指针
        result_set_ = true;
        state_ = State::FAILED;
        cv_.notify_one();
        if (completed_callback_) completed_callback_();
    }

    State GetState() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return state_;
    }

    bool IsCompleted() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return state_ == State::COMPLETED || state_ == State::FAILED;
    }

    bool IsCancelled() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return state_ == State::CANCELLED;
    }

    bool WaitFor(std::chrono::milliseconds timeout) override {
        std::unique_lock<std::mutex> lock(mutex_);
        return cv_.wait_for(lock, timeout, [this] { 
            return state_ != State::PENDING; 
        });
    }

    bool Cancel() override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ != State::PENDING) return false;
        cancelled_ = true;
        result_set_ = true;
        state_ = State::CANCELLED;
        try {
            promise_.set_exception(std::make_exception_ptr(
                std::runtime_error("operation cancelled")));
        } catch (const std::future_error&) {}
        cv_.notify_one();
        if (cancelled_callback_) cancelled_callback_();
        return true;
    }

    void SetOnCompleted(Callback callback) override {
        std::lock_guard<std::mutex> lock(mutex_);
        completed_callback_ = std::move(callback);
        if (state_ != State::PENDING && completed_callback_) {
            completed_callback_();
        }
    }

    void SetOnCancelled(Callback callback) override {
        std::lock_guard<std::mutex> lock(mutex_);
        cancelled_callback_ = std::move(callback);
        if (state_ == State::CANCELLED && cancelled_callback_) {
            cancelled_callback_();
        }
    }

    T GetResult() override {
        try {
            return future_.get();
        } catch (const std::future_error& e) {
            throw std::runtime_error("Result not available: " + std::string(e.what()));
        }
    }

    std::exception_ptr GetException() override {
        std::lock_guard<std::mutex> lock(mutex_);
        // 优先使用存储的异常指针
        if (stored_exception_) {
            return stored_exception_;
        }
        // 如果状态是 FAILED 但没有存储异常，尝试从 future 获取（但可能会消费状态）
        if (state_ == State::FAILED) {
            try {
                future_.get();
            } catch (...) {
                return std::current_exception();
            }
        }
        return nullptr;
    }

private:
    void waitLoop() {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return state_ != State::PENDING; });
    }

    std::promise<T> promise_;
    std::future<T> future_;
    std::thread wait_thread_;
    std::condition_variable cv_;
    mutable std::mutex mutex_;

    State state_;
    bool cancelled_;
    bool result_set_;
    Callback completed_callback_;
    Callback cancelled_callback_;
    std::exception_ptr stored_exception_;  // 存储异常指针，避免消费 future
};

} // namespace httpserver::core::async
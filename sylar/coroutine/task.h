#ifndef __SYLAR_COROUTINE_TASK_H__
#define __SYLAR_COROUTINE_TASK_H__

#include <coroutine>
#include <exception>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "executor.h"

namespace sylar {
namespace coroutine_detail {

void schedule(Executor* executor, std::coroutine_handle<> handle) noexcept;
Executor* currentExecutor() noexcept;

struct FinalAwaiter {
    bool await_ready() const noexcept { return false; }
    template<class Promise>
    void await_suspend(std::coroutine_handle<Promise> handle) const noexcept {
        if(handle.promise().continuation) {
            schedule(handle.promise().executor, handle.promise().continuation);
        }
    }
    void await_resume() const noexcept {}
};

} // namespace coroutine_detail

template<class T>
class Task {
public:
    struct promise_type;
    using Handle = std::coroutine_handle<promise_type>;

    Task() noexcept = default;
    explicit Task(Handle handle) noexcept :m_handle(handle) {}
    Task(Task&& other) noexcept :m_handle(std::exchange(other.m_handle, {})) {}
    Task& operator=(Task&& other) noexcept {
        if(this != &other) {
            destroy();
            m_handle = std::exchange(other.m_handle, {});
        }
        return *this;
    }
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    ~Task() { destroy(); }

    bool valid() const noexcept { return static_cast<bool>(m_handle); }
    bool done() const noexcept { return !m_handle || m_handle.done(); }

    void start(Executor& executor) {
        if(!m_handle || m_handle.promise().started) {
            return;
        }
        m_handle.promise().started = true;
        m_handle.promise().executor = &executor;
        executor.schedule(m_handle);
    }

    /** @brief Resume a task synchronously; useful for deterministic tests. */
    void resume() {
        if(m_handle && !m_handle.done()) {
            m_handle.promise().started = true;
            m_handle.resume();
        }
    }

    T get() {
        if(!m_handle) {
            throw std::logic_error("get() on an empty Task");
        }
        if(!m_handle.done()) {
            resume();
        }
        if(!m_handle.done()) {
            throw std::logic_error("Task is suspended on an external event");
        }
        if(m_handle.promise().exception) {
            std::rethrow_exception(m_handle.promise().exception);
        }
        return std::move(*m_handle.promise().value);
    }

    struct Awaiter {
        Handle handle;
        bool await_ready() const noexcept {
            return !handle || handle.done();
        }
        void await_suspend(std::coroutine_handle<> continuation) {
            auto& promise = handle.promise();
            promise.continuation = continuation;
            promise.started = true;
            if(auto* executor = coroutine_detail::currentExecutor()) {
                promise.executor = executor;
                executor->schedule(handle);
            } else {
                handle.resume();
            }
        }
        T await_resume() {
            if(handle.promise().exception) {
                std::rethrow_exception(handle.promise().exception);
            }
            return std::move(*handle.promise().value);
        }
    };

    Awaiter operator co_await() & noexcept { return Awaiter{m_handle}; }
    Awaiter operator co_await() && noexcept { return Awaiter{m_handle}; }

    struct promise_type {
        std::optional<T> value;
        std::exception_ptr exception;
        std::coroutine_handle<> continuation;
        Executor* executor = nullptr;
        bool started = false;

        Task get_return_object() noexcept {
            return Task(Handle::from_promise(*this));
        }
        std::suspend_always initial_suspend() const noexcept { return {}; }
        coroutine_detail::FinalAwaiter final_suspend() const noexcept { return {}; }
        void unhandled_exception() noexcept { exception = std::current_exception(); }
        template<class Value>
        void return_value(Value&& result) {
            value.emplace(std::forward<Value>(result));
        }
    };

private:
    void destroy() noexcept {
        if(m_handle) {
            m_handle.destroy();
            m_handle = {};
        }
    }

    Handle m_handle;
};

template<>
class Task<void> {
public:
    struct promise_type;
    using Handle = std::coroutine_handle<promise_type>;

    Task() noexcept = default;
    explicit Task(Handle handle) noexcept :m_handle(handle) {}
    Task(Task&& other) noexcept :m_handle(std::exchange(other.m_handle, {})) {}
    Task& operator=(Task&& other) noexcept {
        if(this != &other) {
            destroy();
            m_handle = std::exchange(other.m_handle, {});
        }
        return *this;
    }
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    ~Task() { destroy(); }

    bool valid() const noexcept { return static_cast<bool>(m_handle); }
    bool done() const noexcept { return !m_handle || m_handle.done(); }

    void start(Executor& executor) {
        if(!m_handle || m_handle.promise().started) {
            return;
        }
        m_handle.promise().started = true;
        m_handle.promise().executor = &executor;
        executor.schedule(m_handle);
    }

    void resume() {
        if(m_handle && !m_handle.done()) {
            m_handle.promise().started = true;
            m_handle.resume();
        }
    }

    void get() {
        if(!m_handle) {
            throw std::logic_error("get() on an empty Task");
        }
        if(!m_handle.done()) {
            resume();
        }
        if(!m_handle.done()) {
            throw std::logic_error("Task is suspended on an external event");
        }
        if(m_handle.promise().exception) {
            std::rethrow_exception(m_handle.promise().exception);
        }
    }

    struct Awaiter {
        Handle handle;
        bool await_ready() const noexcept {
            return !handle || handle.done();
        }
        void await_suspend(std::coroutine_handle<> continuation) {
            auto& promise = handle.promise();
            promise.continuation = continuation;
            promise.started = true;
            if(auto* executor = coroutine_detail::currentExecutor()) {
                promise.executor = executor;
                executor->schedule(handle);
            } else {
                handle.resume();
            }
        }
        void await_resume() {
            if(handle.promise().exception) {
                std::rethrow_exception(handle.promise().exception);
            }
        }
    };

    Awaiter operator co_await() & noexcept { return Awaiter{m_handle}; }
    Awaiter operator co_await() && noexcept { return Awaiter{m_handle}; }

    struct promise_type {
        std::exception_ptr exception;
        std::coroutine_handle<> continuation;
        Executor* executor = nullptr;
        bool started = false;

        Task get_return_object() noexcept {
            return Task(Handle::from_promise(*this));
        }
        std::suspend_always initial_suspend() const noexcept { return {}; }
        coroutine_detail::FinalAwaiter final_suspend() const noexcept { return {}; }
        void unhandled_exception() noexcept { exception = std::current_exception(); }
        void return_void() const noexcept {}
    };

private:
    void destroy() noexcept {
        if(m_handle) {
            m_handle.destroy();
            m_handle = {};
        }
    }

    Handle m_handle;
};

} // namespace sylar

#endif

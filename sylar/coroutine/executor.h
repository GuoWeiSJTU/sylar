#ifndef __SYLAR_COROUTINE_EXECUTOR_H__
#define __SYLAR_COROUTINE_EXECUTOR_H__

#include <atomic>
#include <condition_variable>
#include <coroutine>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include "../noncopyable.h"

namespace sylar {

/** @brief C++20 coroutine work executor backed by jthreads. */
class Executor : Noncopyable {
public:
    explicit Executor(size_t thread_count = 1);
    ~Executor();

    void start(size_t thread_count = 0);
    void schedule(std::coroutine_handle<> handle);
    void post(std::function<void()> callback);
    void requestStop();
    void join();

    bool stopping() const noexcept { return m_stopping.load(); }
    size_t pending() const;

    /** @brief Executor currently resuming a coroutine on this thread. */
    static Executor* current() noexcept;

private:
    struct WorkItem {
        std::coroutine_handle<> handle;
        std::function<void()> callback;
    };

    void run(std::stop_token token);

    mutable std::mutex m_mutex;
    std::condition_variable_any m_condition;
    std::queue<WorkItem> m_queue;
    std::vector<std::jthread> m_threads;
    std::atomic<bool> m_stopping{false};
    bool m_started = false;
};

} // namespace sylar

#endif

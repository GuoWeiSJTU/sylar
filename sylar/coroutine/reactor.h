#ifndef __SYLAR_COROUTINE_REACTOR_H__
#define __SYLAR_COROUTINE_REACTOR_H__

#include <atomic>
#include <chrono>
#include <coroutine>
#include <cstdint>
#include <functional>
#include <system_error>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>
#include <unordered_map>
#include <vector>

#include <sys/epoll.h>

#include "executor.h"
#include "task.h"
#include "timer_queue.h"

namespace sylar {

/**
 * @brief One-shot epoll reactor.  It never resumes a coroutine inline from
 * epoll_wait; completions are scheduled on the associated Executor.
 */
class Reactor : Noncopyable {
    struct WaitState;
public:
    using Clock = std::chrono::steady_clock;
    using Events = uint32_t;
    static constexpr Events Readable = EPOLLIN;
    static constexpr Events Writable = EPOLLOUT;

    explicit Reactor(Executor& executor);
    ~Reactor();

    void start();
    void stop();

    class WaitAwaitable {
    public:
        WaitAwaitable(Reactor& reactor, int fd, Events events,
                      std::chrono::milliseconds timeout,
                      std::stop_token stop);
        bool await_ready() const noexcept { return false; }
        void await_suspend(std::coroutine_handle<> handle);
        void await_resume();

    private:
        Reactor* m_reactor;
        int m_fd;
        Events m_events;
        std::chrono::milliseconds m_timeout;
        std::stop_token m_stop;
        std::shared_ptr<WaitState> m_state;
    };

    WaitAwaitable wait(int fd, Events events,
                       std::chrono::milliseconds timeout =
                           std::chrono::milliseconds::max());
    WaitAwaitable wait(int fd, Events events, std::chrono::milliseconds timeout,
                       std::stop_token stop);
    WaitAwaitable waitUntil(int fd, Events events, Clock::time_point deadline,
                            std::stop_token stop = {});

private:
    struct WaitState {
        using StopCallback = std::stop_callback<std::function<void()> >;
        int fd = -1;
        Events events = 0;
        uint32_t generation = 0;
        std::coroutine_handle<> handle;
        Executor* executor = nullptr;
        std::chrono::milliseconds timeout = std::chrono::milliseconds::max();
        std::stop_token stop_token;
        std::atomic<bool> completed{false};
        std::error_code error;
        TimerQueue::TimerHandle timer;
        std::unique_ptr<StopCallback> cancellation;
    };

    void registerWait(const std::shared_ptr<WaitState>& state);
    void complete(const std::shared_ptr<WaitState>& state,
                  std::error_code error = {});
    void run(std::stop_token token);
    void wake();
    void drainWakeup();

    int m_epoll = -1;
    int m_wakeup = -1;
    Executor& m_executor;
    TimerQueue m_timers;
    std::mutex m_mutex;
    std::unordered_map<int, std::shared_ptr<WaitState> > m_waiters;
    uint32_t m_next_generation = 0;
    std::jthread m_thread;
    std::atomic<bool> m_running{false};
};

} // namespace sylar

#endif

#ifndef __SYLAR_COROUTINE_TIMER_QUEUE_H__
#define __SYLAR_COROUTINE_TIMER_QUEUE_H__

#include <chrono>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <vector>

namespace sylar {

/** @brief Thread-safe monotonic timer queue for Reactor and application code. */
class TimerQueue {
public:
    using Clock = std::chrono::steady_clock;
    using Duration = std::chrono::milliseconds;

    struct Entry {
        uint64_t sequence = 0;
        Clock::time_point deadline;
        std::function<void()> callback;
        std::atomic<bool> cancelled{false};
    };

    class TimerHandle {
    public:
        TimerHandle() = default;
        explicit TimerHandle(std::shared_ptr<Entry> entry)
            :m_entry(std::move(entry)) {}
        void cancel() const {
            if(m_entry) {
                m_entry->cancelled.store(true, std::memory_order_release);
            }
        }
        explicit operator bool() const noexcept { return !!m_entry; }
    private:
        std::shared_ptr<Entry> m_entry;
    };

    TimerHandle scheduleAfter(Duration delay, std::function<void()> callback);
    std::vector<std::function<void()> > popExpired(
        Clock::time_point now = Clock::now());
    std::optional<Duration> nextTimeout(
        Clock::time_point now = Clock::now()) const;
    bool empty() const;

private:
    struct Compare {
        bool operator()(const std::shared_ptr<Entry>& lhs,
                        const std::shared_ptr<Entry>& rhs) const {
            if(lhs->deadline != rhs->deadline) {
                return lhs->deadline > rhs->deadline;
            }
            return lhs->sequence > rhs->sequence;
        }
    };

    mutable std::mutex m_mutex;
    mutable std::priority_queue<std::shared_ptr<Entry>,
                        std::vector<std::shared_ptr<Entry> >, Compare> m_queue;
    uint64_t m_sequence = 0;
};

} // namespace sylar

#endif

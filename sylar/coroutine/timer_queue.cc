#include "timer_queue.h"

namespace sylar {

TimerQueue::TimerHandle TimerQueue::scheduleAfter(Duration delay,
                                                   std::function<void()> callback) {
    auto entry = std::make_shared<Entry>();
    entry->deadline = Clock::now() + delay;
    entry->callback = std::move(callback);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        entry->sequence = m_sequence++;
        m_queue.push(entry);
    }
    return TimerHandle(std::move(entry));
}

std::vector<std::function<void()> > TimerQueue::popExpired(Clock::time_point now) {
    std::vector<std::function<void()> > callbacks;
    std::lock_guard<std::mutex> lock(m_mutex);
    while(!m_queue.empty()) {
        auto entry = m_queue.top();
        if(entry->deadline > now) {
            break;
        }
        m_queue.pop();
        if(!entry->cancelled.load(std::memory_order_acquire) && entry->callback) {
            callbacks.push_back(std::move(entry->callback));
        }
    }
    return callbacks;
}

std::optional<TimerQueue::Duration> TimerQueue::nextTimeout(
    Clock::time_point now) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    while(!m_queue.empty() &&
          m_queue.top()->cancelled.load(std::memory_order_acquire)) {
        m_queue.pop();
    }
    if(m_queue.empty()) {
        return std::nullopt;
    }
    if(m_queue.top()->deadline <= now) {
        return Duration(0);
    }
    return std::chrono::duration_cast<Duration>(m_queue.top()->deadline - now);
}

bool TimerQueue::empty() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_queue.empty();
}

} // namespace sylar

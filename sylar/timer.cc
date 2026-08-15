#include "timer.h"
#include "util.h"

namespace sylar {

bool Timer::Comparator::operator()(const Timer::ptr& lhs
                        ,const Timer::ptr& rhs) const {
    if(!lhs && !rhs) {
        return false;
    }
    if(!lhs) {
        return true;
    }
    if(!rhs) {
        return false;
    }
    if(lhs->m_next < rhs->m_next) {
        return true;
    }
    if(rhs->m_next < lhs->m_next) {
        return false;
    }
    return lhs.get() < rhs.get();
}


Timer::Timer(Duration interval, std::function<void()> cb,
             bool recurring, TimerManager* manager)
    :m_recurring(recurring)
    ,m_interval(interval)
    ,m_cb(cb)
    ,m_manager(manager) {
    m_next = Clock::now() + m_interval;
}

Timer::Timer(TimePoint next)
    :m_next(next) {
}

bool Timer::cancel() {
    TimerManager::RWMutexType::WriteLock lock(m_manager->m_mutex);
    if(m_cb) {
        m_cb = nullptr;
        auto it = m_manager->m_timers.find(shared_from_this());
        m_manager->m_timers.erase(it);
        return true;
    }
    return false;
}

bool Timer::refresh() {
    TimerManager::RWMutexType::WriteLock lock(m_manager->m_mutex);
    if(!m_cb) {
        return false;
    }
    auto it = m_manager->m_timers.find(shared_from_this());
    if(it == m_manager->m_timers.end()) {
        return false;
    }
    m_manager->m_timers.erase(it);
    m_next = Clock::now() + m_interval;
    m_manager->m_timers.insert(shared_from_this());
    return true;
}

bool Timer::reset(uint64_t ms, bool from_now) {
    return reset(Duration(ms), from_now);
}

bool Timer::reset(Duration interval, bool from_now) {
    if(interval == m_interval && !from_now) {
        return true;
    }
    TimerManager::RWMutexType::WriteLock lock(m_manager->m_mutex);
    if(!m_cb) {
        return false;
    }
    auto it = m_manager->m_timers.find(shared_from_this());
    if(it == m_manager->m_timers.end()) {
        return false;
    }
    m_manager->m_timers.erase(it);
    if(from_now) {
        auto start = Clock::now();
        m_interval = interval;
        m_next = start + m_interval;
    } else {
        auto start = m_next - m_interval;
        m_interval = interval;
        m_next = start + m_interval;
    }
    m_manager->addTimer(shared_from_this(), lock);
    return true;

}

TimerManager::TimerManager() {
}

TimerManager::~TimerManager() {
}

Timer::ptr TimerManager::addTimer(uint64_t ms, std::function<void()> cb
                                  ,bool recurring) {
    return addTimer(Timer::Duration(ms), std::move(cb), recurring);
}

Timer::ptr TimerManager::addTimer(Timer::Duration interval,
                                  std::function<void()> cb,
                                  bool recurring) {
    Timer::ptr timer(new Timer(interval, std::move(cb), recurring, this));
    RWMutexType::WriteLock lock(m_mutex);
    addTimer(timer, lock);
    return timer;
}

static void OnTimer(std::weak_ptr<void> weak_cond, std::function<void()> cb) {
    std::shared_ptr<void> tmp = weak_cond.lock();
    if(tmp) {
        cb();
    }
}

Timer::ptr TimerManager::addConditionTimer(uint64_t ms, std::function<void()> cb
                                    ,std::weak_ptr<void> weak_cond
                                    ,bool recurring) {
    return addConditionTimer(Timer::Duration(ms), std::move(cb), weak_cond,
                             recurring);
}

Timer::ptr TimerManager::addConditionTimer(Timer::Duration interval,
                                           std::function<void()> cb,
                                           std::weak_ptr<void> weak_cond,
                                           bool recurring) {
    return addTimer(interval,
                    [weak_cond, cb = std::move(cb)] {
                        OnTimer(weak_cond, cb);
                    },
                    recurring);
}

uint64_t TimerManager::getNextTimer() {
    RWMutexType::ReadLock lock(m_mutex);
    m_tickled = false;
    if(m_timers.empty()) {
        return ~0ull;
    }

    const Timer::ptr& next = *m_timers.begin();
    auto now = Timer::Clock::now();
    if(now >= next->m_next) {
        return 0;
    } else {
        return static_cast<uint64_t>(std::chrono::duration_cast<
            Timer::Duration>(next->m_next - now).count());
    }
}

void TimerManager::listExpiredCb(std::vector<std::function<void()> >& cbs) {
    auto now = Timer::Clock::now();
    std::vector<Timer::ptr> expired;
    {
        RWMutexType::ReadLock lock(m_mutex);
        if(m_timers.empty()) {
            return;
        }
    }
    RWMutexType::WriteLock lock(m_mutex);
    if(m_timers.empty()) {
        return;
    }
    if((*m_timers.begin())->m_next > now) {
        return;
    }

    auto it = m_timers.begin();
    while(it != m_timers.end() && (*it)->m_next <= now) {
        ++it;
    }
    expired.insert(expired.begin(), m_timers.begin(), it);
    m_timers.erase(m_timers.begin(), it);
    cbs.reserve(expired.size());

    for(auto& timer : expired) {
        cbs.push_back(timer->m_cb);
        if(timer->m_recurring) {
            timer->m_next = now + timer->m_interval;
            m_timers.insert(timer);
        } else {
            timer->m_cb = nullptr;
        }
    }
}

void TimerManager::addTimer(Timer::ptr val, RWMutexType::WriteLock& lock) {
    auto it = m_timers.insert(val).first;
    bool at_front = (it == m_timers.begin()) && !m_tickled;
    if(at_front) {
        m_tickled = true;
    }
    lock.unlock();

    if(at_front) {
        onTimerInsertedAtFront();
    }
}

bool TimerManager::hasTimer() {
    RWMutexType::ReadLock lock(m_mutex);
    return !m_timers.empty();
}

}

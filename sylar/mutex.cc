#include "mutex.h"
#include "macro.h"
#include "scheduler.h"

namespace sylar {

Semaphore::Semaphore(uint32_t count)
    :m_semaphore(static_cast<std::ptrdiff_t>(count)) {
}

Semaphore::~Semaphore() = default;

void Semaphore::wait() {
    m_semaphore.acquire();
}

void Semaphore::notify() {
    m_semaphore.release();
}

FiberSemaphore::FiberSemaphore(size_t initial_concurrency)
    :m_concurrency(initial_concurrency) {
}

FiberSemaphore::~FiberSemaphore() {
    SYLAR_ASSERT(m_waiters.empty());
}

bool FiberSemaphore::tryWait() {
    SYLAR_ASSERT(Scheduler::GetThis());
    {
        MutexType::Lock lock(m_mutex);
        if(m_concurrency > 0u) {
            --m_concurrency;
            return true;
        }
        return false;
    }
}

void FiberSemaphore::wait() {
    SYLAR_ASSERT(Scheduler::GetThis());
    {
        MutexType::Lock lock(m_mutex);
        if(m_concurrency > 0u) {
            --m_concurrency;
            return;
        }
        m_waiters.push_back(std::make_pair(Scheduler::GetThis(), Fiber::GetThis()));
    }
    Fiber::YieldToHold();
}

void FiberSemaphore::notify() {
    MutexType::Lock lock(m_mutex);
    if(!m_waiters.empty()) {
        auto next = m_waiters.front();
        m_waiters.pop_front();
        next.first->schedule(next.second);
    } else {
        ++m_concurrency;
    }
}

}

#include "reactor.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <system_error>
#include <sys/eventfd.h>
#include <unistd.h>

namespace sylar {

Reactor::WaitAwaitable::WaitAwaitable(Reactor& reactor, int fd, Events events,
                                      std::chrono::milliseconds timeout,
                                      std::stop_token stop)
    :m_reactor(&reactor)
    ,m_fd(fd)
    ,m_events(events)
    ,m_timeout(timeout)
    ,m_stop(stop) {
}

void Reactor::WaitAwaitable::await_suspend(std::coroutine_handle<> handle) {
    m_state = std::make_shared<WaitState>();
    m_state->fd = m_fd;
    m_state->events = m_events;
    m_state->handle = handle;
    m_state->timeout = m_timeout;
    m_state->stop_token = m_stop;
    m_state->executor = Executor::current();
    if(!m_state->executor) {
        m_state->executor = &m_reactor->m_executor;
    }
    m_reactor->registerWait(m_state);
}

void Reactor::WaitAwaitable::await_resume() {
    if(m_state && m_state->error) {
        throw std::system_error(m_state->error);
    }
}

Reactor::Reactor(Executor& executor)
    :m_executor(executor) {
    m_epoll = ::epoll_create1(EPOLL_CLOEXEC);
    m_wakeup = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if(m_epoll < 0 || m_wakeup < 0) {
        int error = errno;
        if(m_epoll >= 0) {
            ::close(m_epoll);
        }
        if(m_wakeup >= 0) {
            ::close(m_wakeup);
        }
        throw std::system_error(error, std::generic_category(),
                                "create reactor");
    }
    epoll_event event{};
    event.events = EPOLLIN;
    event.data.u64 = 0;
    if(::epoll_ctl(m_epoll, EPOLL_CTL_ADD, m_wakeup, &event) != 0) {
        int error = errno;
        ::close(m_wakeup);
        ::close(m_epoll);
        throw std::system_error(error, std::generic_category(),
                                "register reactor wakeup");
    }
    start();
}

Reactor::~Reactor() {
    stop();
    if(m_wakeup >= 0) {
        ::close(m_wakeup);
    }
    if(m_epoll >= 0) {
        ::close(m_epoll);
    }
}

void Reactor::start() {
    bool expected = false;
    if(!m_running.compare_exchange_strong(expected, true)) {
        return;
    }
    m_thread = std::jthread([this](std::stop_token token) { run(token); });
}

void Reactor::stop() {
    bool expected = true;
    if(!m_running.compare_exchange_strong(expected, false)) {
        return;
    }
    m_thread.request_stop();
    wake();
    // Joining occurs when m_thread is destroyed or assigned.  Move it to a
    // local so no user callback runs while the reactor object is torn down.
    std::jthread thread = std::move(m_thread);

    std::vector<std::shared_ptr<WaitState> > pending;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for(auto& item : m_waiters) {
            pending.push_back(item.second);
        }
    }
    for(auto& state : pending) {
        complete(state, std::make_error_code(std::errc::operation_canceled));
    }
}

Reactor::WaitAwaitable Reactor::wait(int fd, Events events,
                                     std::chrono::milliseconds timeout) {
    return WaitAwaitable(*this, fd, events, timeout, {});
}

Reactor::WaitAwaitable Reactor::wait(int fd, Events events,
                                     std::chrono::milliseconds timeout,
                                     std::stop_token stop) {
    return WaitAwaitable(*this, fd, events, timeout, stop);
}

Reactor::WaitAwaitable Reactor::waitUntil(int fd, Events events,
                                          Clock::time_point deadline,
                                          std::stop_token stop) {
    auto timeout = std::chrono::milliseconds::max();
    if(deadline != Clock::time_point::max()) {
        const auto now = Clock::now();
        timeout = now >= deadline
            ? std::chrono::milliseconds(0)
            : std::chrono::duration_cast<std::chrono::milliseconds>(
                  deadline - now);
    }
    return WaitAwaitable(*this, fd, events, timeout, stop);
}

void Reactor::registerWait(const std::shared_ptr<WaitState>& state) {
    std::shared_ptr<WaitState> old_state;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto old = m_waiters.find(state->fd);
        if(old != m_waiters.end()) {
            old_state = old->second;
        }
        m_waiters[state->fd] = state;
        state->generation = ++m_next_generation;
        if(state->generation == 0) {
            state->generation = ++m_next_generation;
        }
        epoll_event event{};
        event.events = state->events | EPOLLERR | EPOLLHUP | EPOLLONESHOT;
        event.data.u64 = (static_cast<uint64_t>(state->generation) << 32) |
                         static_cast<uint32_t>(state->fd);
        if(::epoll_ctl(m_epoll, EPOLL_CTL_ADD, state->fd, &event) != 0 &&
           errno == EEXIST) {
            ::epoll_ctl(m_epoll, EPOLL_CTL_MOD, state->fd, &event);
        }
    }
    if(old_state) {
        complete(old_state, std::make_error_code(std::errc::operation_canceled));
    }
    if(state->timeout != std::chrono::milliseconds::max()) {
        std::weak_ptr<WaitState> weak_state = state;
        state->timer = m_timers.scheduleAfter(
            state->timeout, [this, weak_state] {
                if(auto current = weak_state.lock()) {
                    complete(current, std::make_error_code(std::errc::timed_out));
                }
            });
    }
    if(state->stop_token.stop_possible()) {
        std::weak_ptr<WaitState> weak_state = state;
        state->cancellation = std::make_unique<WaitState::StopCallback>(
            state->stop_token, [this, weak_state] {
                if(auto current = weak_state.lock()) {
                    complete(current,
                             std::make_error_code(std::errc::operation_canceled));
                }
            });
    }
    wake();
}

void Reactor::complete(const std::shared_ptr<WaitState>& state,
                       std::error_code error) {
    if(!state || state->completed.exchange(true)) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_waiters.find(state->fd);
        if(it != m_waiters.end() && it->second == state) {
            m_waiters.erase(it);
            ::epoll_ctl(m_epoll, EPOLL_CTL_DEL, state->fd, nullptr);
        }
    }
    state->error = error;
    if(state->executor) {
        state->executor->schedule(state->handle);
    } else if(state->handle) {
        state->handle.resume();
    }
}

void Reactor::wake() {
    if(m_wakeup >= 0) {
        uint64_t value = 1;
        (void)::write(m_wakeup, &value, sizeof(value));
    }
}

void Reactor::drainWakeup() {
    uint64_t value = 0;
    while(::read(m_wakeup, &value, sizeof(value)) == sizeof(value)) {
    }
}

void Reactor::run(std::stop_token token) {
    epoll_event events[64];
    while(!token.stop_requested() && m_running.load()) {
        int timeout = -1;
        if(auto next = m_timers.nextTimeout()) {
            timeout = static_cast<int>(std::min<int64_t>(
                next->count(), std::numeric_limits<int>::max()));
        }
        int count = ::epoll_wait(m_epoll, events, 64, timeout);
        if(count < 0 && errno != EINTR) {
            break;
        }
        for(int i = 0; i < count; ++i) {
            if(events[i].data.u64 == 0) {
                drainWakeup();
                continue;
            }
            int fd = static_cast<int>(events[i].data.u64 & 0xffffffffu);
            uint32_t generation = static_cast<uint32_t>(events[i].data.u64 >> 32);
            std::shared_ptr<WaitState> state;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                auto it = m_waiters.find(fd);
                if(it != m_waiters.end() && it->second->generation == generation) {
                    state = it->second;
                }
            }
            if(state) {
                std::error_code error;
                if(events[i].events & (EPOLLERR | EPOLLHUP)) {
                    error = std::make_error_code(std::errc::io_error);
                }
                complete(state, error);
            }
        }
        for(auto& callback : m_timers.popExpired()) {
            m_executor.post(std::move(callback));
        }
    }
}

} // namespace sylar

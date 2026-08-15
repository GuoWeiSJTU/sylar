#include "executor.h"
#include "task.h"

namespace sylar {
namespace {
thread_local Executor* t_current_executor = nullptr;
}

Executor::Executor(size_t thread_count) {
    start(thread_count);
}

Executor::~Executor() {
    requestStop();
    join();
}

void Executor::start(size_t thread_count) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if(m_started) {
        return;
    }
    if(thread_count == 0) {
        thread_count = std::max<size_t>(1, std::thread::hardware_concurrency());
    }
    m_started = true;
    m_stopping.store(false);
    m_threads.reserve(thread_count);
    for(size_t i = 0; i < thread_count; ++i) {
        m_threads.emplace_back([this](std::stop_token token) { run(token); });
    }
}

void Executor::schedule(std::coroutine_handle<> handle) {
    if(!handle) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if(m_stopping.load()) {
            return;
        }
        m_queue.push(WorkItem{handle, {}});
    }
    m_condition.notify_one();
}

void Executor::post(std::function<void()> callback) {
    if(!callback) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if(m_stopping.load()) {
            return;
        }
        m_queue.push(WorkItem{{}, std::move(callback)});
    }
    m_condition.notify_one();
}

void Executor::requestStop() {
    m_stopping.store(true);
    m_condition.notify_all();
    for(auto& thread : m_threads) {
        thread.request_stop();
    }
}

void Executor::join() {
    std::vector<std::jthread> threads;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        threads.swap(m_threads);
        m_started = false;
    }
    // jthread joins when this local vector is destroyed, outside the lock.
}

size_t Executor::pending() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_queue.size();
}

Executor* Executor::current() noexcept {
    return t_current_executor;
}

void Executor::run(std::stop_token token) {
    t_current_executor = this;
    while(true) {
        WorkItem item;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_condition.wait(lock, token, [this] {
                return m_stopping.load() || !m_queue.empty();
            });
            if(m_queue.empty()) {
                if(m_stopping.load() || token.stop_requested()) {
                    break;
                }
                continue;
            }
            item = std::move(m_queue.front());
            m_queue.pop();
        }
        try {
            if(item.handle) {
                if(!item.handle.done()) {
                    item.handle.resume();
                }
            } else if(item.callback) {
                item.callback();
            }
        } catch(...) {
            // A coroutine stores exceptions in its promise.  A posted callback
            // has no caller to report to; keep the executor thread alive.
        }
    }
    t_current_executor = nullptr;
}

namespace coroutine_detail {
void schedule(Executor* executor, std::coroutine_handle<> handle) noexcept {
    if(!handle) {
        return;
    }
    if(executor) {
        executor->schedule(handle);
    } else {
        handle.resume();
    }
}

Executor* currentExecutor() noexcept {
    return Executor::current();
}
} // namespace coroutine_detail
} // namespace sylar

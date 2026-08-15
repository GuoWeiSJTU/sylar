#ifndef __SYLAR_MUTEX_H__
#define __SYLAR_MUTEX_H__

#include <thread>
#include <functional>
#include <memory>
#include <stdint.h>
#include <atomic>
#include <list>
#include <mutex>
#include <shared_mutex>
#include <semaphore>
#include <limits>

#include "noncopyable.h"
#include "fiber.h"

namespace sylar {

/**
 * @brief 信号量
 */
class Semaphore : Noncopyable {
public:
    /**
     * @brief 构造函数
     * @param[in] count 信号量值的大小
     */
    Semaphore(uint32_t count = 0);

    /**
     * @brief 析构函数
     */
    ~Semaphore();

    /**
     * @brief 获取信号量
     */
    void wait();

    /**
     * @brief 释放信号量
     */
    void notify();
private:
    static constexpr std::ptrdiff_t kMaxCount =
        (std::numeric_limits<std::ptrdiff_t>::max)();
    std::counting_semaphore<kMaxCount> m_semaphore;
};

/**
 * @brief 局部锁的模板实现
 */
template<class T>
struct ScopedLockImpl {
public:
    /**
     * @brief 构造函数
     * @param[in] mutex Mutex
     */
    ScopedLockImpl(T& mutex)
        :m_mutex(mutex) {
        m_mutex.lock();
        m_locked = true;
    }

    /**
     * @brief 析构函数,自动释放锁
     */
    ~ScopedLockImpl() {
        unlock();
    }

    /**
     * @brief 加锁
     */
    void lock() {
        if(!m_locked) {
            m_mutex.lock();
            m_locked = true;
        }
    }

    /**
     * @brief 解锁
     */
    void unlock() {
        if(m_locked) {
            m_mutex.unlock();
            m_locked = false;
        }
    }
private:
    /// mutex
    T& m_mutex;
    /// 是否已上锁
    bool m_locked;
};

/**
 * @brief 局部读锁模板实现
 */
template<class T>
struct ReadScopedLockImpl {
public:
    /**
     * @brief 构造函数
     * @param[in] mutex 读写锁
     */
    ReadScopedLockImpl(T& mutex)
        :m_mutex(mutex) {
        m_mutex.rdlock();
        m_locked = true;
    }

    /**
     * @brief 析构函数,自动释放锁
     */
    ~ReadScopedLockImpl() {
        unlock();
    }

    /**
     * @brief 上读锁
     */
    void lock() {
        if(!m_locked) {
            m_mutex.rdlock();
            m_locked = true;
        }
    }

    /**
     * @brief 释放锁
     */
    void unlock() {
        if(m_locked) {
            m_mutex.rdunlock();
            m_locked = false;
        }
    }
private:
    /// mutex
    T& m_mutex;
    /// 是否已上锁
    bool m_locked;
};

/**
 * @brief 局部写锁模板实现
 */
template<class T>
struct WriteScopedLockImpl {
public:
    /**
     * @brief 构造函数
     * @param[in] mutex 读写锁
     */
    WriteScopedLockImpl(T& mutex)
        :m_mutex(mutex) {
        m_mutex.wrlock();
        m_locked = true;
    }

    /**
     * @brief 析构函数
     */
    ~WriteScopedLockImpl() {
        unlock();
    }

    /**
     * @brief 上写锁
     */
    void lock() {
        if(!m_locked) {
            m_mutex.wrlock();
            m_locked = true;
        }
    }

    /**
     * @brief 解锁
     */
    void unlock() {
        if(m_locked) {
            m_mutex.unlock();
            m_locked = false;
        }
    }
private:
    /// Mutex
    T& m_mutex;
    /// 是否已上锁
    bool m_locked;
};

/**
 * @brief 互斥量
 */
class Mutex : Noncopyable {
public: 
    /// 局部锁
    typedef ScopedLockImpl<Mutex> Lock;

    /**
     * @brief 构造函数
     */
    Mutex() = default;

    /**
     * @brief 析构函数
     */
    ~Mutex() = default;

    /**
     * @brief 加锁
     */
    void lock() {
        m_mutex.lock();
    }

    /**
     * @brief 解锁
     */
    void unlock() {
        m_mutex.unlock();
    }
private:
    /// mutex
    std::mutex m_mutex;
};

/**
 * @brief 空锁(用于调试)
 */
class NullMutex : Noncopyable{
public:
    /// 局部锁
    typedef ScopedLockImpl<NullMutex> Lock;

    /**
     * @brief 构造函数
     */
    NullMutex() {}

    /**
     * @brief 析构函数
     */
    ~NullMutex() {}

    /**
     * @brief 加锁
     */
    void lock() {}

    /**
     * @brief 解锁
     */
    void unlock() {}
};

/**
 * @brief 读写互斥量
 */
class RWMutex : Noncopyable{
public:

    /// 局部读锁
    typedef ReadScopedLockImpl<RWMutex> ReadLock;

    /// 局部写锁
    typedef WriteScopedLockImpl<RWMutex> WriteLock;

    /**
     * @brief 构造函数
     */
    RWMutex() = default;
    
    /**
     * @brief 析构函数
     */
    ~RWMutex() = default;

    /**
     * @brief 上读锁
     */
    void rdlock() {
        m_lock.lock_shared();
    }

    /**
     * @brief 上写锁
     */
    void wrlock() {
        m_lock.lock();
    }

    /**
     * @brief 解锁
     */
    void unlock() {
        m_lock.unlock();
    }

    /** @brief 释放读锁 */
    void rdunlock() {
        m_lock.unlock_shared();
    }
private:
    /// 读写锁
    std::shared_mutex m_lock;
};

/**
 * @brief 空读写锁(用于调试)
 */
class NullRWMutex : Noncopyable {
public:
    /// 局部读锁
    typedef ReadScopedLockImpl<NullRWMutex> ReadLock;
    /// 局部写锁
    typedef WriteScopedLockImpl<NullRWMutex> WriteLock;

    /**
     * @brief 构造函数
     */
    NullRWMutex() {}
    /**
     * @brief 析构函数
     */
    ~NullRWMutex() {}

    /**
     * @brief 上读锁
     */
    void rdlock() {}

    /**
     * @brief 上写锁
     */
    void wrlock() {}
    /**
     * @brief 解锁
     */
    void unlock() {}
    void rdunlock() {}
};

/**
 * @brief 自旋锁
 */
class Spinlock : Noncopyable {
public:
    /// 局部锁
    typedef ScopedLockImpl<Spinlock> Lock;

    /**
     * @brief 构造函数
     */
    Spinlock() : m_mutex(ATOMIC_FLAG_INIT) {}

    /**
     * @brief 析构函数
     */
    ~Spinlock() = default;

    /**
     * @brief 上锁
     */
    void lock() {
        while(m_mutex.test_and_set(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
    }

    /**
     * @brief 解锁
     */
    void unlock() {
        m_mutex.clear(std::memory_order_release);
    }
private:
    /// 自旋锁
    std::atomic_flag m_mutex;
};

/**
 * @brief 原子锁
 */
class CASLock : Noncopyable {
public:
    /// 局部锁
    typedef ScopedLockImpl<CASLock> Lock;

    /**
     * @brief 构造函数
     */
    CASLock() {
        m_mutex.clear();
    }

    /**
     * @brief 析构函数
     */
    ~CASLock() {
    }

    /**
     * @brief 上锁
     */
    void lock() {
        while(std::atomic_flag_test_and_set_explicit(&m_mutex, std::memory_order_acquire));
    }

    /**
     * @brief 解锁
     */
    void unlock() {
        std::atomic_flag_clear_explicit(&m_mutex, std::memory_order_release);
    }
private:
    /// 原子状态
    std::atomic_flag m_mutex;
};

class Scheduler;
class FiberSemaphore : Noncopyable {
public:
    typedef Spinlock MutexType;

    FiberSemaphore(size_t initial_concurrency = 0);
    ~FiberSemaphore();

    bool tryWait();
    void wait();
    void notify();

    size_t getConcurrency() const { return m_concurrency;}
    void reset() { m_concurrency = 0;}
private:
    MutexType m_mutex;
    std::list<std::pair<Scheduler*, Fiber::ptr> > m_waiters;
    size_t m_concurrency;
};



}

#endif

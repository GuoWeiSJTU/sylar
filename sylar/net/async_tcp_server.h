#ifndef __SYLAR_NET_ASYNC_TCP_SERVER_H__
#define __SYLAR_NET_ASYNC_TCP_SERVER_H__

#include <atomic>
#include <cstddef>
#include <list>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>
#include <system_error>
#include <vector>

#include "../address.h"
#include "../coroutine/executor.h"
#include "../coroutine/reactor.h"
#include "../coroutine/task.h"
#include "async_socket.h"

namespace sylar {

/**
 * @brief Coroutine-only TCP server lifecycle.
 *
 * The listener and every accepted connection are owned by the coroutine
 * runtime.  Blocking operations are represented by explicit awaitables and
 * no process-wide syscall interception is used by this class.
 */
class AsyncTcpServer : public std::enable_shared_from_this<AsyncTcpServer>,
                       Noncopyable {
public:
    using ptr = std::shared_ptr<AsyncTcpServer>;

    explicit AsyncTcpServer(size_t thread_count = 0);
    virtual ~AsyncTcpServer();

    virtual bool bind(Address::ptr address, bool ssl = false);
    virtual bool bind(const std::vector<Address::ptr>& addresses,
              std::vector<Address::ptr>& failures, bool ssl = false);
    virtual bool start();
    virtual void stop();

    bool isStopped() const noexcept { return m_stopped.load(); }
    Executor& executor() noexcept { return m_executor; }
    Reactor& reactor() noexcept { return m_reactor; }
    std::vector<int> listenerFds() const;

protected:
    virtual Task<void> handleClient(AsyncSocket::ptr client,
                                    std::stop_token stop) = 0;
    virtual void onClientError(const std::error_code& error);

private:
    Task<void> acceptLoop(AsyncSocket::ptr listener,
                          std::stop_token stop);
    void launchClient(AsyncSocket::ptr client, std::stop_token stop);
    static int createListener(const Address::ptr& address, bool ssl);

    Executor m_executor;
    Reactor m_reactor;
    std::vector<AsyncSocket::ptr> m_listeners;
    std::list<Task<void> > m_acceptTasks;
    std::list<Task<void> > m_clientTasks;
    mutable std::mutex m_mutex;
    std::stop_source m_stop;
    std::atomic<bool> m_stopped{true};
    bool m_bound = false;
};

}

#endif

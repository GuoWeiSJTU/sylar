#ifndef __SYLAR_NET_ASYNC_SOCKET_H__
#define __SYLAR_NET_ASYNC_SOCKET_H__

#include <cerrno>
#include <chrono>
#include <cstddef>
#include <memory>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <system_error>
#include <utility>

#include <netinet/in.h>
#include <sys/socket.h>

#include "../coroutine/reactor.h"
#include "../coroutine/result.h"
#include "../coroutine/task.h"

namespace sylar {

/**
 * @brief Nonblocking socket adapter for the C++20 coroutine runtime.
 *
 * It deliberately owns a raw descriptor rather than the legacy Socket class,
 * allowing the new stack to be adopted incrementally without Fiber/hook
 * dependencies.
 */
class AsyncSocket : public std::enable_shared_from_this<AsyncSocket> {
public:
    using ptr = std::shared_ptr<AsyncSocket>;
    using Clock = std::chrono::steady_clock;

    AsyncSocket(int fd, Reactor& reactor, Executor& executor,
                bool take_ownership = true);
    ~AsyncSocket();

    AsyncSocket(const AsyncSocket&) = delete;
    AsyncSocket& operator=(const AsyncSocket&) = delete;
    AsyncSocket(AsyncSocket&& other) noexcept;
    AsyncSocket& operator=(AsyncSocket&& other) noexcept;

    int fd() const noexcept { return m_fd; }
    void close() noexcept;

    Task<Result<size_t> > read(std::span<std::byte> buffer);
    Task<Result<size_t> > read(std::span<std::byte> buffer,
                               Clock::time_point deadline,
                               std::stop_token stop = {});
    Task<Result<size_t> > write(std::span<const std::byte> buffer);
    Task<Result<size_t> > write(std::span<const std::byte> buffer,
                                Clock::time_point deadline,
                                std::stop_token stop = {});
    Task<Result<void> > writeAll(std::span<const std::byte> buffer);
    Task<Result<void> > writeAll(std::span<const std::byte> buffer,
                                 Clock::time_point deadline,
                                 std::stop_token stop = {});

    Task<Result<ptr> > accept();
    Task<Result<ptr> > accept(Clock::time_point deadline,
                              std::stop_token stop = {});
    Task<Result<void> > connect(const sockaddr* address, socklen_t length);
    Task<Result<void> > connect(const sockaddr* address, socklen_t length,
                                Clock::time_point deadline,
                                std::stop_token stop = {});

private:
    void setNonBlocking();
    Result<void> socketError() const;

    int m_fd = -1;
    Reactor* m_reactor = nullptr;
    Executor* m_executor = nullptr;
    bool m_owns_fd = true;
};

} // namespace sylar

#endif

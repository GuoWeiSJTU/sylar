#include "async_socket.h"

#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace sylar {

AsyncSocket::AsyncSocket(int fd, Reactor& reactor, Executor& executor,
                         bool take_ownership)
    :m_fd(fd)
    ,m_reactor(&reactor)
    ,m_executor(&executor)
    ,m_owns_fd(take_ownership) {
    if(m_fd < 0) {
        throw std::invalid_argument("AsyncSocket requires a valid fd");
    }
    setNonBlocking();
}

AsyncSocket::~AsyncSocket() {
    close();
}

AsyncSocket::AsyncSocket(AsyncSocket&& other) noexcept
    :m_fd(std::exchange(other.m_fd, -1))
    ,m_reactor(other.m_reactor)
    ,m_executor(other.m_executor)
    ,m_owns_fd(std::exchange(other.m_owns_fd, false)) {
}

AsyncSocket& AsyncSocket::operator=(AsyncSocket&& other) noexcept {
    if(this != &other) {
        close();
        m_fd = std::exchange(other.m_fd, -1);
        m_reactor = other.m_reactor;
        m_executor = other.m_executor;
        m_owns_fd = std::exchange(other.m_owns_fd, false);
    }
    return *this;
}

void AsyncSocket::setNonBlocking() {
    int flags = ::fcntl(m_fd, F_GETFL, 0);
    if(flags < 0 || ::fcntl(m_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        throw std::system_error(errno, std::generic_category(),
                                "set socket nonblocking");
    }
}

void AsyncSocket::close() noexcept {
    if(m_fd >= 0 && m_owns_fd) {
        ::close(m_fd);
    }
    m_fd = -1;
}

Result<void> AsyncSocket::socketError() const {
    return Result<void>::fromError(
        std::error_code(errno ? errno : EIO, std::generic_category()));
}

Task<Result<size_t> > AsyncSocket::read(std::span<std::byte> buffer) {
    return read(buffer, Clock::time_point::max());
}

Task<Result<size_t> > AsyncSocket::read(std::span<std::byte> buffer,
                                        Clock::time_point deadline,
                                        std::stop_token stop) {
    while(true) {
        if(stop.stop_requested()) {
            co_return Result<size_t>::fromError(
                std::make_error_code(std::errc::operation_canceled));
        }
        if(deadline != Clock::time_point::max() && Clock::now() >= deadline) {
            co_return Result<size_t>::fromError(
                std::make_error_code(std::errc::timed_out));
        }
        ssize_t count = static_cast<ssize_t>(::syscall(
            SYS_read, m_fd, buffer.data(), buffer.size()));
        if(count >= 0) {
            co_return Result<size_t>(static_cast<size_t>(count));
        }
        if(errno == EINTR) {
            continue;
        }
        if(errno == EAGAIN || errno == EWOULDBLOCK) {
            try {
                co_await m_reactor->waitUntil(m_fd, Reactor::Readable,
                                              deadline, stop);
            } catch(const std::system_error& error) {
                co_return Result<size_t>::fromError(error.code());
            }
            continue;
        }
        co_return Result<size_t>::fromError(
            std::error_code(errno, std::generic_category()));
    }
}

Task<Result<size_t> > AsyncSocket::write(std::span<const std::byte> buffer) {
    return write(buffer, Clock::time_point::max());
}

Task<Result<size_t> > AsyncSocket::write(std::span<const std::byte> buffer,
                                         Clock::time_point deadline,
                                         std::stop_token stop) {
    while(true) {
        if(stop.stop_requested()) {
            co_return Result<size_t>::fromError(
                std::make_error_code(std::errc::operation_canceled));
        }
        if(deadline != Clock::time_point::max() && Clock::now() >= deadline) {
            co_return Result<size_t>::fromError(
                std::make_error_code(std::errc::timed_out));
        }
        ssize_t count = static_cast<ssize_t>(::syscall(
            SYS_write, m_fd, buffer.data(), buffer.size()));
        if(count >= 0) {
            co_return Result<size_t>(static_cast<size_t>(count));
        }
        if(errno == EINTR) {
            continue;
        }
        if(errno == EAGAIN || errno == EWOULDBLOCK) {
            try {
                co_await m_reactor->waitUntil(m_fd, Reactor::Writable,
                                              deadline, stop);
            } catch(const std::system_error& error) {
                co_return Result<size_t>::fromError(error.code());
            }
            continue;
        }
        co_return Result<size_t>::fromError(
            std::error_code(errno, std::generic_category()));
    }
}

Task<Result<void> > AsyncSocket::writeAll(std::span<const std::byte> buffer) {
    return writeAll(buffer, Clock::time_point::max());
}

Task<Result<void> > AsyncSocket::writeAll(std::span<const std::byte> buffer,
                                          Clock::time_point deadline,
                                          std::stop_token stop) {
    size_t offset = 0;
    while(offset < buffer.size()) {
        auto result = co_await write(buffer.subspan(offset), deadline, stop);
        if(!result) {
            co_return Result<void>::fromError(result.error());
        }
        if(result.value() == 0) {
            co_return Result<void>::fromError(
                std::make_error_code(std::errc::broken_pipe));
        }
        offset += result.value();
    }
    co_return Result<void>();
}

Task<Result<AsyncSocket::ptr> > AsyncSocket::accept() {
    return accept(Clock::time_point::max());
}

Task<Result<AsyncSocket::ptr> > AsyncSocket::accept(
    Clock::time_point deadline, std::stop_token stop) {
    while(true) {
        if(stop.stop_requested()) {
            co_return Result<ptr>::fromError(
                std::make_error_code(std::errc::operation_canceled));
        }
        if(deadline != Clock::time_point::max() && Clock::now() >= deadline) {
            co_return Result<ptr>::fromError(
                std::make_error_code(std::errc::timed_out));
        }
        int accepted = static_cast<int>(::syscall(
            SYS_accept4, m_fd, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC));
        if(accepted >= 0) {
            co_return Result<ptr>(std::make_shared<AsyncSocket>(
                accepted, *m_reactor, *m_executor));
        }
        if(errno == EINTR) {
            continue;
        }
        if(errno == EAGAIN || errno == EWOULDBLOCK) {
            try {
                co_await m_reactor->waitUntil(m_fd, Reactor::Readable,
                                              deadline, stop);
            } catch(const std::system_error& error) {
                co_return Result<ptr>::fromError(error.code());
            }
            continue;
        }
        co_return Result<ptr>::fromError(
            std::error_code(errno, std::generic_category()));
    }
}

Task<Result<void> > AsyncSocket::connect(const sockaddr* address,
                                         socklen_t length) {
    return connect(address, length, Clock::time_point::max());
}

Task<Result<void> > AsyncSocket::connect(const sockaddr* address,
                                         socklen_t length,
                                         Clock::time_point deadline,
                                         std::stop_token stop) {
    while(true) {
        if(stop.stop_requested()) {
            co_return Result<void>::fromError(
                std::make_error_code(std::errc::operation_canceled));
        }
        if(::syscall(SYS_connect, m_fd, address, length) == 0 ||
           errno == EISCONN) {
            co_return Result<void>();
        }
        if(errno == EINTR) {
            continue;
        }
        if(errno != EINPROGRESS && errno != EALREADY) {
            co_return Result<void>::fromError(
                std::error_code(errno, std::generic_category()));
        }
        break;
    }
    try {
        co_await m_reactor->waitUntil(m_fd, Reactor::Writable, deadline, stop);
    } catch(const std::system_error& error) {
        co_return Result<void>::fromError(error.code());
    }
    int error = 0;
    socklen_t error_length = sizeof(error);
    if(::syscall(SYS_getsockopt, m_fd, SOL_SOCKET, SO_ERROR, &error,
                 &error_length) != 0) {
        co_return socketError();
    }
    if(error != 0) {
        co_return Result<void>::fromError(
            std::error_code(error, std::generic_category()));
    }
    co_return Result<void>();
}

} // namespace sylar

#include "async_tcp_server.h"

#include <cerrno>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <system_error>
#include <sys/socket.h>
#include <unistd.h>

#include "../log.h"

namespace sylar {

namespace {
static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("async_server");

bool setNonBlocking(int fd) {
    int flags = ::fcntl(fd, F_GETFL, 0);
    return flags >= 0 && ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}
}

AsyncTcpServer::AsyncTcpServer(size_t thread_count)
    :m_executor(thread_count)
    ,m_reactor(m_executor) {
}

AsyncTcpServer::~AsyncTcpServer() {
    stop();
}

int AsyncTcpServer::createListener(const Address::ptr& address, bool ssl) {
    if(!address || ssl) {
        // TLS requires an explicit coroutine handshake layer; silently
        // falling back to plaintext would be a protocol/security violation.
        return -1;
    }
    int fd = ::socket(address->getFamily(), SOCK_STREAM | SOCK_CLOEXEC, 0);
    if(fd < 0) {
        return -1;
    }
    int reuse = 1;
    if(::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) != 0) goto fail;
    if(!setNonBlocking(fd)) goto fail;
    if(::bind(fd, address->getAddr(), address->getAddrLen()) != 0) goto fail;
    if(::listen(fd, SOMAXCONN) != 0) goto fail;
    return fd;
fail:
    { int error = errno; ::close(fd); errno = error; return -1; }
}

bool AsyncTcpServer::bind(Address::ptr address, bool ssl) {
    std::vector<Address::ptr> addresses;
    std::vector<Address::ptr> failures;
    if(address) {
        addresses.push_back(std::move(address));
    }
    return bind(addresses, failures, ssl);
}

bool AsyncTcpServer::bind(const std::vector<Address::ptr>& addresses,
                          std::vector<Address::ptr>& failures, bool ssl) {
    if(m_stop.stop_requested() || !m_stopped.load() || m_bound || addresses.empty()) {
        return false;
    }
    for(const auto& address : addresses) {
        int fd = createListener(address, ssl);
        if(fd < 0) {
            failures.push_back(address);
            SYLAR_LOG_ERROR(g_logger) << "async listener bind failed address="
                                      << (address ? address->toString() : "null")
                                      << " errno=" << errno;
            continue;
        }
        m_listeners.push_back(std::make_shared<AsyncSocket>(
            fd, m_reactor, m_executor));
    }
    if(!failures.empty() || m_listeners.empty()) {
        for(auto& listener : m_listeners) {
            listener->close();
        }
        m_listeners.clear();
        return false;
    }
    m_bound = true;
    return true;
}

bool AsyncTcpServer::start() {
    if(m_stop.stop_requested() || !m_bound || !m_stopped.exchange(false)) {
        return m_bound && !m_stopped.load();
    }
    const auto stop = m_stop.get_token();
    for(const auto& listener : m_listeners) {
        m_acceptTasks.emplace_back(acceptLoop(listener, stop));
        m_acceptTasks.back().start(m_executor);
    }
    return true;
}

void AsyncTcpServer::stop() {
    if(m_stopped.exchange(true)) {
        return;
    }
    m_stop.request_stop();
    for(auto& listener : m_listeners) {
        listener->close();
    }
    // Reactor completion schedules cancellation continuations while the
    // Executor is still alive.  Join the reactor before stopping workers.
    m_reactor.stop();
    m_executor.requestStop();
    m_executor.join();
    std::lock_guard<std::mutex> lock(m_mutex);
    m_clientTasks.clear();
    m_acceptTasks.clear();
    m_listeners.clear();
    m_bound = false;
}

std::vector<int> AsyncTcpServer::listenerFds() const {
    std::vector<int> result;
    for(const auto& listener : m_listeners) {
        if(listener && listener->fd() >= 0) {
            result.push_back(listener->fd());
        }
    }
    return result;
}

Task<void> AsyncTcpServer::acceptLoop(AsyncSocket::ptr listener,
                                      std::stop_token stop) {
    while(!stop.stop_requested() && listener && listener->fd() >= 0) {
        auto accepted = co_await listener->accept(
            AsyncSocket::Clock::time_point::max(), stop);
        if(!accepted) {
            if(stop.stop_requested() ||
               accepted.error() ==
                   std::make_error_code(std::errc::operation_canceled)) {
                co_return;
            }
            onClientError(accepted.error());
            continue;
        }
        launchClient(std::move(accepted.value()), stop);
    }
}

void AsyncTcpServer::launchClient(AsyncSocket::ptr client,
                                  std::stop_token stop) {
    auto task = handleClient(std::move(client), stop);
    std::lock_guard<std::mutex> lock(m_mutex);
    m_clientTasks.emplace_back(std::move(task));
    m_clientTasks.back().start(m_executor);
}

void AsyncTcpServer::onClientError(const std::error_code& error) {
    if(error != std::make_error_code(std::errc::operation_canceled)) {
        SYLAR_LOG_ERROR(g_logger) << "async accept failed: " << error.message();
    }
}

}

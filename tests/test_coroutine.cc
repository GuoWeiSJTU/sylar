#include "sylar/coroutine/executor.h"
#include "sylar/coroutine/reactor.h"
#include "sylar/coroutine/task.h"
#include "sylar/coroutine/timer_queue.h"
#include "sylar/http/async_http_session.h"
#include "sylar/http/async_websocket_session.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <array>
#include <cassert>
#include <future>
#include <iostream>
#include <cstring>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>

using namespace sylar;

static Task<int> make_value() {
    co_return 42;
}

static Task<void> await_value(std::promise<int>& result) {
    auto value = co_await make_value();
    result.set_value(value);
    co_return;
}

static Task<void> await_pipe(Reactor& reactor, int fd,
                             std::promise<char>& result) {
    co_await reactor.wait(fd, Reactor::Readable);
    char value = 0;
    assert(::read(fd, &value, 1) == 1);
    result.set_value(value);
    co_return;
}

static Task<void> await_http(http::AsyncHttpSession& session,
                             std::promise<std::string>& result) {
    auto request = co_await session.receiveRequest();
    result.set_value(request ? request.value()->getPath() : "error");
    co_return;
}

static Task<void> await_websocket(http::AsyncWebSocketSession& session,
                                  std::promise<std::string>& result) {
    auto message = co_await session.receiveMessage();
    if(!message) {
        result.set_value("error");
        co_return;
    }
    auto sent = co_await session.sendMessage(*message.value());
    result.set_value(sent ? message.value()->getData() : "send-error");
    co_return;
}

static Task<void> await_timeout(Reactor& reactor, int fd,
                                std::promise<char>& result) {
    try {
        co_await reactor.wait(fd, Reactor::Readable,
                              std::chrono::milliseconds(20));
        result.set_value('n');
    } catch(const std::system_error&) {
        result.set_value('t');
    }
    co_return;
}

static Task<void> await_cancel(Reactor& reactor, int fd,
                               std::stop_token stop,
                               std::promise<char>& result) {
    try {
        co_await reactor.wait(fd, Reactor::Readable,
                              std::chrono::milliseconds::max(), stop);
        result.set_value('n');
    } catch(const std::system_error&) {
        result.set_value('c');
    }
    co_return;
}

static Task<void> await_socket_read(
    AsyncSocket& socket, std::promise<char>& result,
    AsyncSocket::Clock::time_point deadline, std::stop_token stop = {}) {
    std::array<std::byte, 1> buffer{};
    auto value = co_await socket.read(std::span<std::byte>(buffer), deadline,
                                     stop);
    if(value) {
        result.set_value('r');
    } else if(value.error() == std::make_error_code(std::errc::timed_out)) {
        result.set_value('t');
    } else if(value.error() ==
              std::make_error_code(std::errc::operation_canceled)) {
        result.set_value('c');
    } else {
        result.set_value('e');
    }
    co_return;
}

int main() {
    {
        Executor executor(2);
        std::promise<int> result;
        auto future = result.get_future();
        auto task = await_value(result);
        task.start(executor);
        assert(future.wait_for(std::chrono::seconds(2)) ==
               std::future_status::ready);
        assert(future.get() == 42);
    }

    TimerQueue timers;
    bool fired = false;
    timers.scheduleAfter(std::chrono::milliseconds(0), [&] { fired = true; });
    for(auto& callback : timers.popExpired()) {
        callback();
    }
    assert(fired);

    int pipe_fds[2] = {-1, -1};
    assert(::pipe(pipe_fds) == 0);
    {
        Executor executor(1);
        Reactor reactor(executor);
        std::promise<char> result;
        auto future = result.get_future();
        auto task = await_pipe(reactor, pipe_fds[0], result);
        task.start(executor);
        const char value = 'x';
        assert(::write(pipe_fds[1], &value, 1) == 1);
        assert(future.wait_for(std::chrono::seconds(2)) ==
               std::future_status::ready);
        assert(future.get() == value);
    }
    ::close(pipe_fds[0]);
    ::close(pipe_fds[1]);

    int timeout_pipe[2] = {-1, -1};
    assert(::pipe(timeout_pipe) == 0);
    {
        Executor executor(1);
        Reactor reactor(executor);
        std::promise<char> result;
        auto future = result.get_future();
        auto task = await_timeout(reactor, timeout_pipe[0], result);
        task.start(executor);
        assert(future.wait_for(std::chrono::seconds(2)) ==
               std::future_status::ready);
        assert(future.get() == 't');
    }
    ::close(timeout_pipe[0]);
    ::close(timeout_pipe[1]);

    int cancel_pipe[2] = {-1, -1};
    assert(::pipe(cancel_pipe) == 0);
    {
        Executor executor(1);
        Reactor reactor(executor);
        std::stop_source source;
        std::promise<char> result;
        auto future = result.get_future();
        auto task = await_cancel(reactor, cancel_pipe[0],
                                 source.get_token(), result);
        task.start(executor);
        source.request_stop();
        assert(future.wait_for(std::chrono::seconds(2)) ==
               std::future_status::ready);
        assert(future.get() == 'c');
    }
    ::close(cancel_pipe[0]);
    ::close(cancel_pipe[1]);

    int async_socket_fds[2] = {-1, -1};
    assert(::socketpair(AF_UNIX, SOCK_STREAM, 0, async_socket_fds) == 0);
    {
        Executor executor(1);
        Reactor reactor(executor);
        auto socket = std::make_shared<AsyncSocket>(async_socket_fds[0],
                                                     reactor, executor);
        std::promise<char> result;
        auto future = result.get_future();
        auto task = await_socket_read(
            *socket, result,
            AsyncSocket::Clock::now() + std::chrono::milliseconds(20));
        task.start(executor);
        assert(future.wait_for(std::chrono::seconds(2)) ==
               std::future_status::ready);
        assert(future.get() == 't');
    }
    ::close(async_socket_fds[1]);

    int async_cancel_fds[2] = {-1, -1};
    assert(::socketpair(AF_UNIX, SOCK_STREAM, 0, async_cancel_fds) == 0);
    {
        Executor executor(1);
        Reactor reactor(executor);
        auto socket = std::make_shared<AsyncSocket>(async_cancel_fds[0],
                                                     reactor, executor);
        std::stop_source source;
        std::promise<char> result;
        auto future = result.get_future();
        auto task = await_socket_read(
            *socket, result, AsyncSocket::Clock::time_point::max(),
            source.get_token());
        task.start(executor);
        source.request_stop();
        assert(future.wait_for(std::chrono::seconds(2)) ==
               std::future_status::ready);
        assert(future.get() == 'c');
    }
    ::close(async_cancel_fds[1]);

    int websocket_fds[2] = {-1, -1};
    assert(::socketpair(AF_UNIX, SOCK_STREAM, 0, websocket_fds) == 0);
    {
        Executor executor(1);
        Reactor reactor(executor);
        auto socket = std::make_shared<AsyncSocket>(websocket_fds[0],
                                                     reactor, executor);
        http::AsyncWebSocketSession session(socket);
        std::promise<std::string> result;
        auto future = result.get_future();
        const unsigned char frame[] = {
            0x81, 0x82, 0x01, 0x02, 0x03, 0x04,
            static_cast<unsigned char>('h' ^ 0x01),
            static_cast<unsigned char>('i' ^ 0x02)};
        assert(::syscall(SYS_write, websocket_fds[1], frame, sizeof(frame)) ==
               static_cast<ssize_t>(sizeof(frame)));
        auto task = await_websocket(session, result);
        task.start(executor);
        assert(future.wait_for(std::chrono::seconds(2)) ==
               std::future_status::ready);
        assert(future.get() == "hi");
        unsigned char response[4] = {};
        size_t received = 0;
        while(received < sizeof(response)) {
            ssize_t count = static_cast<ssize_t>(::syscall(
                SYS_read, websocket_fds[1], response + received,
                sizeof(response) - received));
            assert(count > 0);
            received += static_cast<size_t>(count);
        }
        assert(response[0] == 0x81 && response[1] == 0x02 &&
               response[2] == 'h' && response[3] == 'i');
    }
    ::close(websocket_fds[1]);

    int socket_fds[2] = {-1, -1};
    assert(::socketpair(AF_UNIX, SOCK_STREAM, 0, socket_fds) == 0);
    {
        Executor executor(1);
        Reactor reactor(executor);
        auto socket = std::make_shared<AsyncSocket>(socket_fds[0], reactor,
                                                     executor);
        http::AsyncHttpSession session(socket);
        std::promise<std::string> result;
        auto future = result.get_future();
        const std::string request =
            "GET /cpp20 HTTP/1.1\r\nHost: localhost\r\n"
            "Content-Length: 0\r\n\r\n";
        ssize_t sent = static_cast<ssize_t>(::syscall(
            SYS_write, socket_fds[1], request.data(), request.size()));
        if(sent != static_cast<ssize_t>(request.size())) {
            std::cerr << "send failed fd=" << socket_fds[1] << " errno="
                      << errno << " " << std::strerror(errno) << std::endl;
            return 1;
        }
        auto task = await_http(session, result);
        task.start(executor);
        assert(future.wait_for(std::chrono::seconds(2)) ==
               std::future_status::ready);
        assert(future.get() == "/cpp20");
    }
    ::close(socket_fds[1]);
    return 0;
}

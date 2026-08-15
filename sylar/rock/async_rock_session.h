#ifndef __SYLAR_ROCK_ASYNC_ROCK_SESSION_H__
#define __SYLAR_ROCK_ASYNC_ROCK_SESSION_H__

#include <chrono>
#include <memory>
#include <stop_token>

#include "rock_protocol.h"
#include "sylar/coroutine/result.h"
#include "sylar/coroutine/task.h"
#include "sylar/net/async_socket.h"

namespace sylar {

class AsyncRockSession {
public:
    using Clock = std::chrono::steady_clock;
    explicit AsyncRockSession(AsyncSocket::ptr socket);

    Task<Result<Message::ptr>> receive(Clock::time_point deadline,
                                       std::stop_token stop = {});
    Task<Result<void>> send(Message::ptr message, Clock::time_point deadline,
                             std::stop_token stop = {});

private:
    Task<Result<std::string>> readExact(size_t size, Clock::time_point deadline,
                                        std::stop_token stop);
    AsyncSocket::ptr m_socket;
    std::string m_pending;
};

}

#endif

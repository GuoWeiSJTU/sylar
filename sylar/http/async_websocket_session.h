#ifndef __SYLAR_HTTP_ASYNC_WEBSOCKET_SESSION_H__
#define __SYLAR_HTTP_ASYNC_WEBSOCKET_SESSION_H__

#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <stop_token>

#include "ws_session.h"
#include "../coroutine/result.h"
#include "../coroutine/task.h"
#include "../net/async_socket.h"

namespace sylar {
namespace http {

/** @brief Coroutine WebSocket frame transport (handshake is HTTP-layer). */
class AsyncWebSocketSession {
public:
    using ptr = std::shared_ptr<AsyncWebSocketSession>;
    using Clock = std::chrono::steady_clock;

    explicit AsyncWebSocketSession(AsyncSocket::ptr socket);

    Task<Result<WSFrameMessage::ptr> > receiveMessage();
    Task<Result<WSFrameMessage::ptr> > receiveMessage(
        Clock::time_point deadline, std::stop_token stop = {});
    Task<Result<void> > sendMessage(const WSFrameMessage& message,
                                    bool fin = true,
                                    Clock::time_point deadline =
                                        Clock::time_point::max(),
                                    std::stop_token stop = {});
    Task<Result<void> > ping(
        Clock::time_point deadline = Clock::time_point::max(),
        std::stop_token stop = {});
    Task<Result<void> > pong(
        Clock::time_point deadline = Clock::time_point::max(),
        std::stop_token stop = {});

private:
    Task<Result<std::string> > readExact(size_t size,
                                         Clock::time_point deadline,
                                         std::stop_token stop);
    Task<Result<void> > sendFrame(uint8_t opcode, const std::string& payload,
                                  bool fin, Clock::time_point deadline,
                                  std::stop_token stop);

    AsyncSocket::ptr m_socket;
    std::string m_pending;
    std::string m_fragment;
    int m_fragment_opcode = 0;
};

} // namespace http
} // namespace sylar

#endif

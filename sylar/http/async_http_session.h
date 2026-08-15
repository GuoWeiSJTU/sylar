#ifndef __SYLAR_HTTP_ASYNC_HTTP_SESSION_H__
#define __SYLAR_HTTP_ASYNC_HTTP_SESSION_H__

#include <chrono>
#include <memory>
#include <string>
#include <stop_token>
#include <vector>

#include "http_parser.h"
#include "../coroutine/result.h"
#include "../coroutine/task.h"
#include "../net/async_socket.h"

namespace sylar {
namespace http {

/** @brief HTTP/1.1 session implemented on AsyncSocket and Task. */
class AsyncHttpSession {
public:
    using ptr = std::shared_ptr<AsyncHttpSession>;
    using Clock = std::chrono::steady_clock;

    explicit AsyncHttpSession(AsyncSocket::ptr socket);

    AsyncSocket::ptr socket() const { return m_socket; }

    Task<Result<HttpRequest::ptr> > receiveRequest();
    Task<Result<HttpRequest::ptr> > receiveRequest(Clock::time_point deadline,
                                                   std::stop_token stop = {});
    Task<Result<void> > sendResponse(HttpResponse::ptr response);
    Task<Result<void> > sendResponse(HttpResponse::ptr response,
                                     Clock::time_point deadline,
                                     std::stop_token stop = {});

private:
    Result<HttpRequest::ptr> protocolError() const;

    AsyncSocket::ptr m_socket;
    std::vector<char> m_buffer;
    size_t m_pending = 0;
};

} // namespace http
} // namespace sylar

#endif

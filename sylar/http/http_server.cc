#include "http_server.h"

#include "sylar/http/servlets/config_servlet.h"
#include "sylar/http/servlets/status_servlet.h"
#include "sylar/log.h"

namespace sylar { namespace http {

namespace { Logger::ptr g_logger = SYLAR_LOG_NAME("http_server"); }

HttpServer::HttpServer(bool keepalive, size_t thread_count)
    :TcpServer(thread_count)
    ,m_keepalive(keepalive)
    ,m_dispatch(std::make_shared<ServletDispatch>()) {
    m_type = "http";
    m_dispatch->addServlet("/_/status", std::make_shared<StatusServlet>());
    m_dispatch->addServlet("/_/config", std::make_shared<ConfigServlet>());
}

void HttpServer::setName(const std::string& value) {
    TcpServer::setName(value);
    m_dispatch->setDefault(std::make_shared<NotFoundServlet>(value));
}

Task<void> HttpServer::handleClient(AsyncSocket::ptr client,
                                    std::stop_token stop) {
    AsyncHttpSession session(std::move(client));
    auto legacy_session = std::make_shared<HttpSession>(nullptr, false);
    const auto deadline = AsyncHttpSession::Clock::now() +
                          std::chrono::milliseconds(m_recv_timeout);
    while(!stop.stop_requested()) {
        auto request = co_await session.receiveRequest(deadline, stop);
        if(!request) {
            SYLAR_LOG_DEBUG(g_logger) << "HTTP receive failed: "
                                      << request.error().message();
            co_return;
        }
        auto response = std::make_shared<HttpResponse>(request.value()->getVersion(),
            request.value()->isClose() || !m_keepalive);
        response->setHeader("Server", getName());
        m_dispatch->handle(request.value(), response, legacy_session);
        auto sent = co_await session.sendResponse(response, deadline, stop);
        if(!sent || !m_keepalive || request.value()->isClose()) co_return;
    }
}

}}

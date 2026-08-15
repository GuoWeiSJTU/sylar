#ifndef __SYLAR_HTTP_HTTP_SERVER_H__
#define __SYLAR_HTTP_HTTP_SERVER_H__

#include "sylar/tcp_server.h"
#include "async_http_session.h"
#include "http_session.h"
#include "servlet.h"

namespace sylar { namespace http {

class HttpServer : public TcpServer {
public:
    using ptr = std::shared_ptr<HttpServer>;
    explicit HttpServer(bool keepalive = false, size_t thread_count = 0);

    ServletDispatch::ptr getServletDispatch() const { return m_dispatch; }
    void setServletDispatch(ServletDispatch::ptr value) { m_dispatch = std::move(value); }
    void setName(const std::string& value) override;

protected:
    Task<void> handleClient(AsyncSocket::ptr client,
                            std::stop_token stop) override;

private:
    bool m_keepalive;
    ServletDispatch::ptr m_dispatch;
};

}}

#endif

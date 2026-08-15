#ifndef __SYLAR_HTTP_WS_SERVER_H__
#define __SYLAR_HTTP_WS_SERVER_H__

#include "sylar/tcp_server.h"
#include "async_http_session.h"
#include "async_websocket_session.h"
#include "ws_servlet.h"

namespace sylar { namespace http {

class WSServer : public TcpServer {
public:
    using ptr = std::shared_ptr<WSServer>;
    explicit WSServer(size_t thread_count = 0);
    WSServletDispatch::ptr getWSServletDispatch() const { return m_dispatch; }
    void setWSServletDispatch(WSServletDispatch::ptr value) { m_dispatch = std::move(value); }

protected:
    Task<void> handleClient(AsyncSocket::ptr client,
                            std::stop_token stop) override;
private:
    WSServletDispatch::ptr m_dispatch;
};

}}

#endif

#include "ws_server.h"

#include <algorithm>
#include <cctype>

#include "sylar/log.h"
#include "sylar/util/hash_util.h"

namespace sylar { namespace http {

namespace { Logger::ptr g_logger = SYLAR_LOG_NAME("websocket_server");

bool equalsIgnoreCase(std::string left, std::string right) {
    std::transform(left.begin(), left.end(), left.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::transform(right.begin(), right.end(), right.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return left == right;
}
}

WSServer::WSServer(size_t thread_count)
    :TcpServer(thread_count)
    ,m_dispatch(std::make_shared<WSServletDispatch>()) {
    m_type = "websocket_server";
}

Task<void> WSServer::handleClient(AsyncSocket::ptr client,
                                  std::stop_token stop) {
    AsyncHttpSession http(std::move(client));
    auto header_result = co_await http.receiveRequest(
        AsyncHttpSession::Clock::now() + std::chrono::milliseconds(m_recv_timeout), stop);
    if(!header_result) co_return;
    auto header = header_result.value();
    if(!equalsIgnoreCase(header->getHeader("Upgrade"), "websocket") ||
       !equalsIgnoreCase(header->getHeader("Connection"), "Upgrade") ||
       header->getHeaderAs<int>("Sec-WebSocket-Version") != 13) co_return;
    const std::string key = header->getHeader("Sec-WebSocket-Key");
    if(key.empty()) co_return;
    auto response = header->createResponse();
    response->setStatus(HttpStatus::SWITCHING_PROTOCOLS);
    response->setWebsocket(true);
    response->setReason("Web Socket Protocol Handshake");
    response->setHeader("Upgrade", "websocket");
    response->setHeader("Connection", "Upgrade");
    response->setHeader("Sec-WebSocket-Accept",
        base64encode(sha1sum(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11")));
    if(!(co_await http.sendResponse(response))) co_return;

    auto servlet = m_dispatch->getWSServlet(header->getPath());
    if(!servlet) co_return;
    auto legacy = std::make_shared<WSSession>(nullptr, false);
    if(servlet->onConnect(header, legacy)) co_return;
    AsyncWebSocketSession websocket(http.socket());
    while(!stop.stop_requested()) {
        auto message = co_await websocket.receiveMessage(
            AsyncWebSocketSession::Clock::now() + std::chrono::milliseconds(m_recv_timeout), stop);
        if(!message) break;
        if(servlet->handle(header, message.value(), legacy)) break;
    }
    servlet->onClose(header, legacy);
}

}}

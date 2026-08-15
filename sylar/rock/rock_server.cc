#include "rock_server.h"

#include "sylar/log.h"

namespace sylar {

namespace { Logger::ptr g_logger = SYLAR_LOG_NAME("rock_server"); }

RockServer::RockServer(const std::string& type, size_t thread_count)
    :TcpServer(thread_count) {
    m_type = type;
}

Task<void> RockServer::handleClient(AsyncSocket::ptr client,
                                    std::stop_token stop) {
    AsyncRockSession session(std::move(client));
    while(!stop.stop_requested()) {
        auto message = co_await session.receive(
            AsyncRockSession::Clock::now() + std::chrono::milliseconds(m_recv_timeout), stop);
        if(!message) {
            SYLAR_LOG_DEBUG(g_logger) << "Rock receive failed: " << message.error().message();
            co_return;
        }
        if(message.value()->getType() == Message::REQUEST) {
            auto request = std::dynamic_pointer_cast<RockRequest>(message.value());
            auto response = request->createResponse();
            response->setResult(0);
            response->setResultStr("ok");
            if(!(co_await session.send(response,
                    AsyncRockSession::Clock::now() + std::chrono::milliseconds(m_recv_timeout), stop))) co_return;
        }
    }
}

}

#ifndef __SYLAR_ROCK_ROCK_SERVER_H__
#define __SYLAR_ROCK_ROCK_SERVER_H__

#include "async_rock_session.h"
#include "sylar/tcp_server.h"

namespace sylar {

class RockServer : public TcpServer {
public:
    using ptr = std::shared_ptr<RockServer>;
    explicit RockServer(const std::string& type = "rock", size_t thread_count = 0);

protected:
    Task<void> handleClient(AsyncSocket::ptr client,
                            std::stop_token stop) override;
};

}

#endif

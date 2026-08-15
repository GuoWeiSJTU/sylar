#include "sylar/http/http_server.h"
#include "sylar/address.h"

#include <cassert>

int main() {
    auto server = std::make_shared<sylar::http::HttpServer>(true, 1);
    auto address = sylar::IPAddress::Create("127.0.0.1", 0);
    assert(address);
    assert(server->bind(address));
    assert(!server->listenerFds().empty());
    assert(server->start());
    server->stop();
    assert(server->isStopped());
    return 0;
}

#include "sylar/tcp_server.h"
#include "sylar/net/async_socket.h"
#include "sylar/coroutine/task.h"
#include "sylar/address.h"

#include <array>
#include <chrono>
#include <thread>

class EchoServer : public sylar::TcpServer {
public:
    explicit EchoServer(size_t threads = 0) : sylar::TcpServer(threads) {}
protected:
    sylar::Task<void> handleClient(sylar::AsyncSocket::ptr client,
                                   std::stop_token stop) override {
        std::array<std::byte, 4096> buffer{};
        while(!stop.stop_requested()) {
            auto result = co_await client->read(std::span<std::byte>(buffer.data(), buffer.size()),
                sylar::AsyncSocket::Clock::now() + std::chrono::seconds(30), stop);
            if(!result || result.value() == 0) co_return;
            if(!(co_await client->writeAll(std::span<const std::byte>(buffer.data(), result.value()),
                    sylar::AsyncSocket::Clock::now() + std::chrono::seconds(30), stop))) co_return;
        }
    }
};

int main() {
    auto server = std::make_shared<EchoServer>(2);
    auto address = sylar::Address::LookupAny("0.0.0.0:8020");
    if(!server->bind(address) || !server->start()) return 1;
    while(!server->isStopped()) std::this_thread::sleep_for(std::chrono::seconds(1));
    return 0;
}

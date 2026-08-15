#include "sylar/http/http_server.h"
#include "sylar/address.h"

#include <chrono>
#include <thread>

int main() {
    auto server = std::make_shared<sylar::http::HttpServer>(true, 2);
    auto address = sylar::Address::LookupAny("0.0.0.0:8020");
    if(!server->bind(address) || !server->start()) return 1;
    auto dispatch = server->getServletDispatch();
    dispatch->addServlet("/hello", [](sylar::http::HttpRequest::ptr,
                                      sylar::http::HttpResponse::ptr response,
                                      sylar::http::HttpSession::ptr) {
        response->setBody("hello from sylar coroutine runtime\n");
        return 0;
    });
    while(!server->isStopped()) std::this_thread::sleep_for(std::chrono::seconds(1));
    return 0;
}

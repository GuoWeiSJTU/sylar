#include "tcp_server.h"

#include "config.h"
#include "log.h"

namespace sylar {

namespace {
ConfigVar<uint64_t>::ptr g_tcp_server_read_timeout =
    Config::Lookup("tcp_server.read_timeout", uint64_t(60 * 1000 * 2),
                   "tcp server read timeout");
Logger::ptr g_logger = SYLAR_LOG_NAME("system");
}

TcpServer::TcpServer(size_t thread_count)
    :AsyncTcpServer(thread_count)
    ,m_recv_timeout(g_tcp_server_read_timeout->getValue())
    ,m_name("sylar/2.0.0") {
}

bool TcpServer::bind(Address::ptr address, bool ssl) {
    m_ssl = ssl;
    return AsyncTcpServer::bind(std::move(address), ssl);
}

bool TcpServer::bind(const std::vector<Address::ptr>& addresses,
                     std::vector<Address::ptr>& failures, bool ssl) {
    m_ssl = ssl;
    return AsyncTcpServer::bind(addresses, failures, ssl);
}

bool TcpServer::start() {
    if(m_ssl) {
        SYLAR_LOG_ERROR(g_logger) << "TLS is not available in the coroutine transport; "
                                  << "configure an explicit TLS adapter";
        return false;
    }
    return AsyncTcpServer::start();
}

void TcpServer::stop() {
    AsyncTcpServer::stop();
}

bool TcpServer::loadCertificates(const std::string&, const std::string&) {
    return !m_ssl;
}

void TcpServer::setConf(const TcpServerConf& value) {
    m_conf = std::make_shared<TcpServerConf>(value);
    if(value.async_runtime != "coroutine") {
        SYLAR_LOG_WARN(g_logger) << "ignoring removed async_runtime=" << value.async_runtime;
    }
}

std::string TcpServer::toString(const std::string& prefix) const {
    std::stringstream stream;
    stream << prefix << "[type=" << m_type << " name=" << m_name
           << " ssl=" << m_ssl << " runtime=coroutine"
           << " recv_timeout=" << m_recv_timeout << " listeners=";
    auto fds = listenerFds();
    for(size_t i = 0; i < fds.size(); ++i) {
        if(i) stream << ',';
        stream << fds[i];
    }
    stream << "]\n";
    return stream.str();
}

}

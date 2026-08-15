#include "async_http_session.h"

#include <cerrno>
#include <cstring>
#include <sstream>

namespace sylar {
namespace http {

AsyncHttpSession::AsyncHttpSession(AsyncSocket::ptr socket)
    :m_socket(std::move(socket))
    ,m_buffer(HttpRequestParser::GetHttpRequestBufferSize()) {
}

Result<HttpRequest::ptr> AsyncHttpSession::protocolError() const {
    return Result<HttpRequest::ptr>::fromError(
        std::make_error_code(std::errc::protocol_error));
}

Task<Result<HttpRequest::ptr> > AsyncHttpSession::receiveRequest() {
    return receiveRequest(Clock::time_point::max());
}

Task<Result<HttpRequest::ptr> > AsyncHttpSession::receiveRequest(
    Clock::time_point deadline, std::stop_token stop) {
    if(!m_socket) {
        co_return Result<HttpRequest::ptr>::fromError(
            std::make_error_code(std::errc::bad_file_descriptor));
    }
    HttpRequestParser parser;
    size_t used = m_pending;
    m_pending = 0;
    while(!parser.isFinished()) {
        if(used == m_buffer.size()) {
            co_return protocolError();
        }
        auto bytes = std::span<std::byte>(
            reinterpret_cast<std::byte*>(m_buffer.data() + used),
            m_buffer.size() - used);
        auto result = co_await m_socket->read(bytes, deadline, stop);
        if(!result) {
            co_return Result<HttpRequest::ptr>::fromError(result.error());
        }
        if(result.value() == 0) {
            co_return Result<HttpRequest::ptr>::fromError(
                std::make_error_code(std::errc::connection_reset));
        }
        used += result.value();
        size_t consumed = parser.execute(m_buffer.data(), used);
        used -= consumed;
        if(parser.hasError()) {
            co_return protocolError();
        }
    }

    uint64_t content_length = parser.getContentLength();
    if(content_length > HttpRequestParser::GetHttpRequestMaxBodySize()) {
        co_return protocolError();
    }
    std::string body;
    body.reserve(static_cast<size_t>(content_length));
    size_t copied = std::min<size_t>(used, static_cast<size_t>(content_length));
    body.append(m_buffer.data(), copied);
    if(used > copied) {
        const size_t pending = used - copied;
        std::memmove(m_buffer.data(), m_buffer.data() + copied, pending);
        m_pending = pending;
    }
    while(body.size() < content_length) {
        auto bytes = std::span<std::byte>(
            reinterpret_cast<std::byte*>(m_buffer.data()), m_buffer.size());
        auto result = co_await m_socket->read(bytes, deadline, stop);
        if(!result) {
            co_return Result<HttpRequest::ptr>::fromError(result.error());
        }
        if(result.value() == 0) {
            co_return Result<HttpRequest::ptr>::fromError(
                std::make_error_code(std::errc::connection_reset));
        }
        size_t remaining = static_cast<size_t>(content_length) - body.size();
        size_t received = result.value();
        size_t take = std::min(remaining, received);
        body.append(m_buffer.data(), take);
        if(received > take) {
            m_pending = received - take;
            std::memmove(m_buffer.data(), m_buffer.data() + take, m_pending);
        }
    }
    if(content_length > 0) {
        parser.getData()->setBody(std::move(body));
    }
    parser.getData()->init();
    co_return Result<HttpRequest::ptr>(parser.getData());
}

Task<Result<void> > AsyncHttpSession::sendResponse(HttpResponse::ptr response) {
    return sendResponse(response, Clock::time_point::max());
}

Task<Result<void> > AsyncHttpSession::sendResponse(
    HttpResponse::ptr response, Clock::time_point deadline,
    std::stop_token stop) {
    if(!m_socket || !response) {
        co_return Result<void>::fromError(
            std::make_error_code(std::errc::invalid_argument));
    }
    std::stringstream stream;
    stream << *response;
    const std::string data = stream.str();
    auto bytes = std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(data.data()), data.size());
    co_return co_await m_socket->writeAll(bytes, deadline, stop);
}

} // namespace http
} // namespace sylar

#include "async_rock_session.h"

#include <array>
#include <cstring>
#include <limits>
#include <system_error>

#include "sylar/endian.h"
#include "sylar/config.h"
#include "sylar/streams/zlib_stream.h"

namespace sylar {

namespace {
constexpr uint8_t kGzipFlag = 0x1;
constexpr uint8_t kMagic[] = {0xab, 0xcd};
ConfigVar<uint32_t>::ptr maxLength() {
    static auto value = Config::Lookup("rock.protocol.max_length", uint32_t(64 * 1024 * 1024),
                                       "rock protocol max length");
    return value;
}
ConfigVar<uint32_t>::ptr gzipMinLength() {
    static auto value = Config::Lookup("rock.protocol.gzip_min_length", uint32_t(4 * 1024),
                                       "rock protocol gzip threshold");
    return value;
}
Result<void> error(std::errc value) { return Result<void>::fromError(std::make_error_code(value)); }
}

AsyncRockSession::AsyncRockSession(AsyncSocket::ptr socket)
    :m_socket(std::move(socket)) {
}

Task<Result<std::string>> AsyncRockSession::readExact(size_t size,
                                                       Clock::time_point deadline,
                                                       std::stop_token stop) {
    std::string result;
    result.reserve(size);
    if(!m_pending.empty()) {
        const size_t take = std::min(size, m_pending.size());
        result.append(m_pending.data(), take);
        m_pending.erase(0, take);
    }
    while(result.size() < size) {
        std::array<std::byte, 4096> storage{};
        const size_t wanted = std::min(storage.size(), size - result.size());
        auto read = co_await m_socket->read(std::span<std::byte>(storage.data(), wanted), deadline, stop);
        if(!read) co_return Result<std::string>::fromError(read.error());
        if(read.value() == 0) co_return Result<std::string>::fromError(std::make_error_code(std::errc::connection_reset));
        const size_t take = std::min(wanted, read.value());
        result.append(reinterpret_cast<const char*>(storage.data()), take);
        if(read.value() > take) m_pending.append(reinterpret_cast<const char*>(storage.data() + take), read.value() - take);
    }
    co_return Result<std::string>(std::move(result));
}

Task<Result<Message::ptr>> AsyncRockSession::receive(Clock::time_point deadline,
                                                      std::stop_token stop) {
    if(!m_socket) co_return Result<Message::ptr>::fromError(std::make_error_code(std::errc::bad_file_descriptor));
    auto header_data = co_await readExact(sizeof(RockMsgHeader), deadline, stop);
    if(!header_data) co_return Result<Message::ptr>::fromError(header_data.error());
    RockMsgHeader header{};
    std::memcpy(&header, header_data.value().data(), sizeof(header));
    if(std::memcmp(header.magic, kMagic, sizeof(kMagic)) != 0 || header.version != 1) {
        co_return Result<Message::ptr>::fromError(std::make_error_code(std::errc::protocol_error));
    }
    const int32_t length = byteswapOnLittleEndian(header.length);
    if(length < 1 || static_cast<uint32_t>(length) > maxLength()->getValue()) {
        co_return Result<Message::ptr>::fromError(std::make_error_code(std::errc::message_size));
    }
    auto body_data = co_await readExact(static_cast<size_t>(length), deadline, stop);
    if(!body_data) co_return Result<Message::ptr>::fromError(body_data.error());
    auto body = std::make_shared<ByteArray>();
    body->write(body_data.value().data(), body_data.value().size());
    body->setPosition(0);
    if(header.flag & kGzipFlag) {
        auto zlib = ZlibStream::CreateGzip(false);
        if(zlib->write(body, -1) != Z_OK || zlib->flush() != Z_OK) {
            co_return Result<Message::ptr>::fromError(std::make_error_code(std::errc::protocol_error));
        }
        body = zlib->getByteArray();
        body->setPosition(0);
    }
    Message::ptr message;
    const uint8_t type = body->readFuint8();
    if(type == Message::REQUEST) message = std::make_shared<RockRequest>();
    else if(type == Message::RESPONSE) message = std::make_shared<RockResponse>();
    else if(type == Message::NOTIFY) message = std::make_shared<RockNotify>();
    else co_return Result<Message::ptr>::fromError(std::make_error_code(std::errc::protocol_error));
    if(!message->parseFromByteArray(body)) co_return Result<Message::ptr>::fromError(std::make_error_code(std::errc::protocol_error));
    co_return Result<Message::ptr>(std::move(message));
}

Task<Result<void>> AsyncRockSession::send(Message::ptr message,
                                           Clock::time_point deadline,
                                           std::stop_token stop) {
    if(!m_socket || !message) co_return error(std::errc::invalid_argument);
    auto body = message->toByteArray();
    if(!body) co_return error(std::errc::invalid_argument);
    body->setPosition(0);
    RockMsgHeader header;
    header.length = static_cast<int32_t>(body->getReadSize());
    if(static_cast<uint32_t>(header.length) >= gzipMinLength()->getValue()) {
        auto zlib = ZlibStream::CreateGzip(true);
        if(zlib->write(body, -1) != Z_OK || zlib->flush() != Z_OK) co_return error(std::errc::io_error);
        body = zlib->getByteArray();
        body->setPosition(0);
        header.flag |= kGzipFlag;
        header.length = static_cast<int32_t>(body->getReadSize());
    }
    header.length = byteswapOnLittleEndian(header.length);
    std::string packet(sizeof(header), '\0');
    std::memcpy(packet.data(), &header, sizeof(header));
    packet.append(body->toString());
    auto bytes = std::span<const std::byte>(reinterpret_cast<const std::byte*>(packet.data()), packet.size());
    co_return co_await m_socket->writeAll(bytes, deadline, stop);
}

}

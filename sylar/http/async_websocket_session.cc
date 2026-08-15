#include "async_websocket_session.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <system_error>

namespace sylar {
namespace http {

namespace {
constexpr uint64_t kMaxWebSocketPayload = 32ull * 1024ull * 1024ull;

Result<void> invalidFrame() {
    return Result<void>::fromError(
        std::make_error_code(std::errc::protocol_error));
}
}

AsyncWebSocketSession::AsyncWebSocketSession(AsyncSocket::ptr socket)
    :m_socket(std::move(socket)) {
}

Task<Result<std::string> > AsyncWebSocketSession::readExact(
    size_t size, Clock::time_point deadline, std::stop_token stop) {
    std::string result;
    result.reserve(size);
    if(!m_pending.empty()) {
        size_t take = std::min(size, m_pending.size());
        result.append(m_pending.data(), take);
        m_pending.erase(0, take);
    }
    while(result.size() < size) {
        std::array<std::byte, 4096> storage{};
        size_t wanted = std::min(storage.size(), size - result.size());
        auto received = co_await m_socket->read(
            std::span<std::byte>(storage.data(), wanted), deadline, stop);
        if(!received) {
            co_return Result<std::string>::fromError(received.error());
        }
        if(received.value() == 0) {
            co_return Result<std::string>::fromError(
                std::make_error_code(std::errc::connection_reset));
        }
        result.append(reinterpret_cast<const char*>(storage.data()),
                     received.value());
    }
    co_return Result<std::string>(std::move(result));
}

Task<Result<WSFrameMessage::ptr> > AsyncWebSocketSession::receiveMessage() {
    return receiveMessage(Clock::time_point::max());
}

Task<Result<WSFrameMessage::ptr> > AsyncWebSocketSession::receiveMessage(
    Clock::time_point deadline, std::stop_token stop) {
    if(!m_socket) {
        co_return Result<WSFrameMessage::ptr>::fromError(
            std::make_error_code(std::errc::bad_file_descriptor));
    }
    auto head_result = co_await readExact(2, deadline, stop);
    if(!head_result) {
        co_return Result<WSFrameMessage::ptr>::fromError(head_result.error());
    }
    std::string head = std::move(head_result.value());
    if(head.size() != 2) {
        co_return Result<WSFrameMessage::ptr>::fromError(
            std::make_error_code(std::errc::protocol_error));
    }
    const uint8_t first = static_cast<uint8_t>(head[0]);
    const uint8_t second = static_cast<uint8_t>(head[1]);
    bool fin = (first & 0x80) != 0;
    uint8_t opcode = first & 0x0f;
    bool masked = (second & 0x80) != 0;
    uint64_t length = second & 0x7f;
    if((first & 0x70) != 0) {
        co_return Result<WSFrameMessage::ptr>::fromError(
            std::make_error_code(std::errc::protocol_error));
    }
    if(length == 126) {
        auto extended_result = co_await readExact(2, deadline, stop);
        if(!extended_result) {
            co_return Result<WSFrameMessage::ptr>::fromError(
                extended_result.error());
        }
        std::string extended = std::move(extended_result.value());
        length = (static_cast<uint8_t>(extended[0]) << 8) |
                 static_cast<uint8_t>(extended[1]);
    } else if(length == 127) {
        auto extended_result = co_await readExact(8, deadline, stop);
        if(!extended_result) {
            co_return Result<WSFrameMessage::ptr>::fromError(
                extended_result.error());
        }
        std::string extended = std::move(extended_result.value());
        length = 0;
        for(unsigned char byte : extended) {
            length = (length << 8) | byte;
        }
        if(length > std::numeric_limits<size_t>::max()) {
            co_return Result<WSFrameMessage::ptr>::fromError(
                std::make_error_code(std::errc::value_too_large));
        }
    }
    if(length > kMaxWebSocketPayload) {
        co_return Result<WSFrameMessage::ptr>::fromError(
            std::make_error_code(std::errc::message_size));
    }
    std::string mask;
    if(masked) {
        auto mask_result = co_await readExact(4, deadline, stop);
        if(!mask_result) {
            co_return Result<WSFrameMessage::ptr>::fromError(mask_result.error());
        }
        mask = std::move(mask_result.value());
    }
    auto payload_result = co_await readExact(static_cast<size_t>(length),
                                             deadline, stop);
    if(!payload_result) {
        co_return Result<WSFrameMessage::ptr>::fromError(payload_result.error());
    }
    std::string payload = std::move(payload_result.value());
    if(masked) {
        for(size_t i = 0; i < payload.size(); ++i) {
            payload[i] ^= mask[i % 4];
        }
    }
    // This class is the server side of the connection; every client frame,
    // including control frames, must carry a mask.
    if(!masked) {
        co_return Result<WSFrameMessage::ptr>::fromError(
            std::make_error_code(std::errc::protocol_error));
    }
    if(opcode >= WSFrameHead::CLOSE && (!fin || length > 125)) {
        co_return Result<WSFrameMessage::ptr>::fromError(
            std::make_error_code(std::errc::protocol_error));
    }
    if(opcode == WSFrameHead::PING) {
        auto pong_result = co_await sendFrame(WSFrameHead::PONG, payload, true,
                                              deadline, stop);
        if(!pong_result) {
            co_return Result<WSFrameMessage::ptr>::fromError(pong_result.error());
        }
        co_return co_await receiveMessage(deadline, stop);
    }
    if(opcode == WSFrameHead::PONG) {
        co_return co_await receiveMessage(deadline, stop);
    }
    if(opcode == WSFrameHead::CLOSE) {
        co_return Result<WSFrameMessage::ptr>::fromError(
            std::make_error_code(std::errc::connection_reset));
    }
    if(opcode != WSFrameHead::CONTINUE && opcode != WSFrameHead::TEXT_FRAME &&
       opcode != WSFrameHead::BIN_FRAME) {
        co_return Result<WSFrameMessage::ptr>::fromError(
            std::make_error_code(std::errc::protocol_error));
    }
    if(opcode != WSFrameHead::CONTINUE) {
        if(!m_fragment.empty()) {
            co_return Result<WSFrameMessage::ptr>::fromError(
                std::make_error_code(std::errc::protocol_error));
        }
        m_fragment.clear();
        m_fragment_opcode = opcode;
    } else if(m_fragment.empty()) {
        co_return Result<WSFrameMessage::ptr>::fromError(
            std::make_error_code(std::errc::protocol_error));
    }
    m_fragment += payload;
    if(!fin) {
        co_return co_await receiveMessage(deadline, stop);
    }
    auto message = std::make_shared<WSFrameMessage>(
        m_fragment_opcode, std::move(m_fragment));
    m_fragment_opcode = 0;
    co_return Result<WSFrameMessage::ptr>(std::move(message));
}

Task<Result<void> > AsyncWebSocketSession::sendFrame(
    uint8_t opcode, const std::string& payload, bool fin,
    Clock::time_point deadline, std::stop_token stop) {
    if(payload.size() > kMaxWebSocketPayload) {
        co_return invalidFrame();
    }
    std::string frame;
    frame.push_back(static_cast<char>((fin ? 0x80 : 0) | (opcode & 0x0f)));
    if(payload.size() < 126) {
        frame.push_back(static_cast<char>(payload.size()));
    } else if(payload.size() <= 0xffff) {
        frame.push_back(126);
        frame.push_back(static_cast<char>((payload.size() >> 8) & 0xff));
        frame.push_back(static_cast<char>(payload.size() & 0xff));
    } else {
        frame.push_back(127);
        for(int shift = 56; shift >= 0; shift -= 8) {
            frame.push_back(static_cast<char>((payload.size() >> shift) & 0xff));
        }
    }
    frame.append(payload);
    auto bytes = std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(frame.data()), frame.size());
    co_return co_await m_socket->writeAll(bytes, deadline, stop);
}

Task<Result<void> > AsyncWebSocketSession::sendMessage(
    const WSFrameMessage& message, bool fin, Clock::time_point deadline,
    std::stop_token stop) {
    co_return co_await sendFrame(static_cast<uint8_t>(message.getOpcode()),
                                 message.getData(), fin, deadline, stop);
}

Task<Result<void> > AsyncWebSocketSession::ping(Clock::time_point deadline,
                                                std::stop_token stop) {
    co_return co_await sendFrame(WSFrameHead::PING, "", true, deadline, stop);
}

Task<Result<void> > AsyncWebSocketSession::pong(Clock::time_point deadline,
                                                std::stop_token stop) {
    co_return co_await sendFrame(WSFrameHead::PONG, "", true, deadline, stop);
}

} // namespace http
} // namespace sylar

#ifndef __SYLAR_COROUTINE_RESULT_H__
#define __SYLAR_COROUTINE_RESULT_H__

#include <optional>
#include <system_error>
#include <utility>

namespace sylar {

/**
 * @brief A small C++20 result type used by asynchronous APIs.
 *
 * It intentionally stores an error_code instead of an exception.  This keeps
 * socket and protocol failures explicit while Task still propagates coding
 * errors through its promise exception channel.
 */
template<class T>
class Result {
public:
    Result(const T& value) :m_value(value) {}
    Result(T&& value) :m_value(std::move(value)) {}
    explicit Result(std::error_code error) :m_error(error) {}

    static Result fromError(std::error_code error) {
        return Result(error);
    }

    bool hasValue() const noexcept { return m_value.has_value(); }
    explicit operator bool() const noexcept { return hasValue(); }
    const std::error_code& error() const noexcept { return m_error; }

    T& value() & {
        ensureValue();
        return *m_value;
    }
    const T& value() const & {
        ensureValue();
        return *m_value;
    }
    T&& value() && {
        ensureValue();
        return std::move(*m_value);
    }

private:
    void ensureValue() const {
        if(!m_value) {
            throw std::system_error(m_error ? m_error
                                             : std::make_error_code(
                                                   std::errc::io_error));
        }
    }

    std::optional<T> m_value;
    std::error_code m_error;
};

template<>
class Result<void> {
public:
    Result() = default;
    explicit Result(std::error_code error) :m_error(error) {}

    static Result fromError(std::error_code error) {
        return Result(error);
    }

    bool hasValue() const noexcept { return !m_error; }
    explicit operator bool() const noexcept { return hasValue(); }
    const std::error_code& error() const noexcept { return m_error; }

    void value() const {
        if(m_error) {
            throw std::system_error(m_error);
        }
    }

private:
    std::error_code m_error;
};

} // namespace sylar

#endif

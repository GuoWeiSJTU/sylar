#ifndef __SYLAR_UTIL_LEXICAL_CAST_H__
#define __SYLAR_UTIL_LEXICAL_CAST_H__

#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace sylar {
namespace detail {

/**
 * @brief Small, dependency-free replacement for boost::lexical_cast.
 *
 * The framework only needs streamable scalar conversions.  Keeping the
 * conversion in one place makes the C++20 migration independent of Boost's
 * header-only implementation while retaining the same throwing contract.
 */
template<class To, class From>
To lexical_cast(const From& value) {
    if constexpr(std::is_same_v<std::decay_t<From>, To>) {
        return value;
    } else {
        std::stringstream stream;
        stream << value;
        To result{};
        stream >> result;
        if(stream.fail()) {
            throw std::invalid_argument("lexical conversion failed");
        }
        stream >> std::ws;
        if(!stream.eof()) {
            throw std::invalid_argument("lexical conversion left trailing data");
        }
        return result;
    }
}

template<class To>
To lexical_cast(const std::string& value) {
    if constexpr(std::is_same_v<To, std::string>) {
        return value;
    } else {
        std::stringstream stream(value);
        To result{};
        stream >> result;
        if(stream.fail()) {
            throw std::invalid_argument("lexical conversion failed");
        }
        stream >> std::ws;
        if(!stream.eof()) {
            throw std::invalid_argument("lexical conversion left trailing data");
        }
        return result;
    }
}

} // namespace detail
} // namespace sylar

#endif

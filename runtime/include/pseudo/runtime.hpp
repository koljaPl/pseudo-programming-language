#pragma once

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace tpp::runtime {
namespace detail {

inline std::int64_t checked_string_length(const std::size_t length)
{
    if (!std::in_range<std::int64_t>(length)) {
        throw std::length_error{"string length exceeds int range"};
    }

    return static_cast<std::int64_t>(length);
}

inline std::size_t checked_string_index(
    const std::string& value,
    const std::int64_t index)
{
    if (!std::in_range<std::size_t>(index)) {
        throw std::out_of_range{"string index out of range"};
    }

    const auto position = static_cast<std::size_t>(index);
    if (position >= value.size()) {
        throw std::out_of_range{"string index out of range"};
    }

    return position;
}

}

inline std::int64_t string_length(const std::string& value)
{
    return detail::checked_string_length(value.size());
}

inline char string_index(
    const std::string& value,
    const std::int64_t index)
{
    return value[detail::checked_string_index(value, index)];
}

inline char& string_index(
    std::string& value,
    const std::int64_t index)
{
    return value[detail::checked_string_index(value, index)];
}

inline std::string substring(
    const std::string& value,
    const std::int64_t left,
    const std::int64_t right)
{
    if (!std::in_range<std::size_t>(left)
        || !std::in_range<std::size_t>(right)
        || right < left) {
        throw std::out_of_range{"substring bounds out of range"};
    }

    const auto first = static_cast<std::size_t>(left);
    const auto last = static_cast<std::size_t>(right);
    if (last > value.size()) {
        throw std::out_of_range{"substring bounds out of range"};
    }

    return value.substr(first, last - first);
}

inline void string_push(std::string& value, const char character)
{
    value.push_back(character);
}

inline std::string read_string()
{
    std::string value;
    if (!(std::cin >> value)) {
        throw std::runtime_error{"failed to read string"};
    }

    return value;
}

inline char read_char()
{
    char value{};
    if (!(std::cin >> value)) {
        throw std::runtime_error{"failed to read char"};
    }

    return value;
}

}

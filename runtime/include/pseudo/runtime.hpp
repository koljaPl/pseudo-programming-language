#pragma once

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

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

template <typename T>
typename std::vector<T>::size_type checked_vector_size(
    const std::int64_t size)
{
    using SizeType = typename std::vector<T>::size_type;

    if (!std::in_range<SizeType>(size)) {
        throw std::length_error{"vector size out of range"};
    }

    const auto count = static_cast<SizeType>(size);
    if (count > std::vector<T>{}.max_size()) {
        throw std::length_error{"vector size out of range"};
    }

    return count;
}

template <typename T>
typename std::vector<T>::size_type checked_vector_index(
    const std::vector<T>& value,
    const std::int64_t index)
{
    using SizeType = typename std::vector<T>::size_type;

    if (!std::in_range<SizeType>(index)) {
        throw std::out_of_range{"vector index out of range"};
    }

    const auto position = static_cast<SizeType>(index);
    if (position >= value.size()) {
        throw std::out_of_range{"vector index out of range"};
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

template <typename T>
std::vector<T> make_vector(const std::int64_t size)
{
    return std::vector<T>(detail::checked_vector_size<T>(size));
}

template <typename T>
std::vector<T> make_vector(
    const std::int64_t size,
    const T& initial_value)
{
    return std::vector<T>(
        detail::checked_vector_size<T>(size),
        initial_value);
}

template <typename T>
typename std::vector<T>::const_reference vector_index(
    const std::vector<T>& value,
    const std::int64_t index)
{
    return value[detail::checked_vector_index(value, index)];
}

template <typename T>
typename std::vector<T>::reference vector_index(
    std::vector<T>& value,
    const std::int64_t index)
{
    return value[detail::checked_vector_index(value, index)];
}

inline std::int64_t read_int()
{
    std::string token;
    if (!(std::cin >> token)) {
        throw std::runtime_error{"failed to read int"};
    }

    std::size_t position = 0;
    bool is_negative = false;
    if (token.front() == '+' || token.front() == '-') {
        is_negative = token.front() == '-';
        position = 1;
    }

    if (position == token.size()) {
        throw std::runtime_error{"failed to read int"};
    }

    for (auto index = position; index < token.size(); ++index) {
        const char character = token[index];
        if (character < '0' || character > '9') {
            throw std::runtime_error{"failed to read int"};
        }
    }

    constexpr auto positive_limit = static_cast<std::uint64_t>(
        std::numeric_limits<std::int64_t>::max());
    constexpr auto negative_limit = positive_limit + std::uint64_t{1};
    const auto limit = is_negative ? negative_limit : positive_limit;

    std::uint64_t magnitude = 0;
    for (; position < token.size(); ++position) {
        const auto digit = static_cast<std::uint64_t>(
            token[position] - '0');
        if (magnitude > (limit - digit) / std::uint64_t{10}) {
            throw std::out_of_range{"integer input out of range"};
        }

        magnitude = magnitude * std::uint64_t{10} + digit;
    }

    if (!is_negative) {
        return static_cast<std::int64_t>(magnitude);
    }
    if (magnitude == negative_limit) {
        return std::numeric_limits<std::int64_t>::min();
    }

    return -static_cast<std::int64_t>(magnitude);
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

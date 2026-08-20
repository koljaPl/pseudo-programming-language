#include "test_support.hpp"

#include "pseudo/runtime.hpp"

#include <cstdint>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

class ScopedCinBuffer {
public:
    explicit ScopedCinBuffer(std::streambuf* replacement)
        : previous_state_{std::cin.rdstate()}
        , previous_exceptions_{std::cin.exceptions()}
        , previous_buffer_{std::cin.rdbuf(replacement)}
    {
        std::cin.exceptions(std::ios::goodbit);
        std::cin.clear();
    }

    ScopedCinBuffer(const ScopedCinBuffer&) = delete;
    ScopedCinBuffer& operator=(const ScopedCinBuffer&) = delete;

    ~ScopedCinBuffer() noexcept
    {
        std::cin.exceptions(std::ios::goodbit);
        std::cin.rdbuf(previous_buffer_);
        std::cin.clear();
        std::cin.exceptions(previous_exceptions_);
        try {
            std::cin.clear(previous_state_);
        } catch (...) {
        }
    }

private:
    std::ios::iostate previous_state_;
    std::ios::iostate previous_exceptions_;
    std::streambuf* previous_buffer_;
};

template <typename Exception, typename Callable>
void check_exception(
    Callable&& callable,
    const std::string_view expected_message)
{
    try {
        callable();
    } catch (const Exception& exception) {
        TPP_CHECK_EQ(
            std::string_view{exception.what()},
            expected_message);
        return;
    }

    throw tpp::test::Failure{"expected runtime exception"};
}

void length_counts_bytes_and_checks_conversion()
{
    const std::string bytes{"A\0B", 3};
    TPP_CHECK_EQ(
        tpp::runtime::string_length(bytes),
        std::int64_t{3});

    if constexpr (
        std::numeric_limits<std::size_t>::max()
        > static_cast<std::size_t>(
            std::numeric_limits<std::int64_t>::max())) {
        check_exception<std::length_error>(
            [] {
                (void)tpp::runtime::detail::checked_string_length(
                    std::numeric_limits<std::size_t>::max());
            },
            "string length exceeds int range");
    }
}

void indexing_reads_and_mutates_bytes()
{
    std::string value{"A\0C", 3};
    const std::string& const_value = value;

    TPP_CHECK_EQ(
        tpp::runtime::string_index(const_value, 1),
        '\0');
    tpp::runtime::string_index(value, 2) = 'B';
    const std::string expected{"A\0B", 3};
    TPP_CHECK_EQ(value, expected);

    for (const auto index : {std::int64_t{-1}, std::int64_t{3}}) {
        check_exception<std::out_of_range>(
            [&] {
                (void)tpp::runtime::string_index(const_value, index);
            },
            "string index out of range");
    }
}

void substring_uses_half_open_checked_bounds()
{
    const std::string value{"A\0BCD", 5};
    const std::string middle{"\0BC", 3};
    TPP_CHECK_EQ(
        tpp::runtime::substring(value, 1, 4),
        middle);
    TPP_CHECK_EQ(
        tpp::runtime::substring(value, 2, 2),
        std::string{});
    TPP_CHECK_EQ(
        tpp::runtime::substring(value, 0, 5),
        value);

    for (const auto& bounds : {
             std::pair{std::int64_t{-1}, std::int64_t{1}},
             std::pair{std::int64_t{2}, std::int64_t{1}},
             std::pair{std::int64_t{0}, std::int64_t{6}},
         }) {
        check_exception<std::out_of_range>(
            [&] {
                (void)tpp::runtime::substring(
                    value,
                    bounds.first,
                    bounds.second);
            },
            "substring bounds out of range");
    }
}

void push_appends_one_byte()
{
    std::string value{"A", 1};
    tpp::runtime::string_push(value, '\0');
    tpp::runtime::string_push(value, 'B');
    const std::string expected{"A\0B", 3};
    TPP_CHECK_EQ(value, expected);
}

void formatted_reads_skip_whitespace()
{
    std::istringstream input{"  hello  x"};
    const ScopedCinBuffer redirected{input.rdbuf()};

    TPP_CHECK_EQ(tpp::runtime::read_string(), std::string{"hello"});
    TPP_CHECK_EQ(tpp::runtime::read_char(), 'x');
}

void failed_reads_throw()
{
    {
        std::istringstream input;
        const ScopedCinBuffer redirected{input.rdbuf()};
        check_exception<std::runtime_error>(
            [] { (void)tpp::runtime::read_string(); },
            "failed to read string");
    }

    {
        std::istringstream input{"   "};
        const ScopedCinBuffer redirected{input.rdbuf()};
        check_exception<std::runtime_error>(
            [] { (void)tpp::runtime::read_char(); },
            "failed to read char");
    }
}

}

int main()
{
    return tpp::test::run({
        {"byte length and overflow", length_counts_bytes_and_checks_conversion},
        {"checked byte indexing", indexing_reads_and_mutates_bytes},
        {"half-open substring", substring_uses_half_open_checked_bounds},
        {"push one byte", push_appends_one_byte},
        {"formatted reads", formatted_reads_skip_whitespace},
        {"read failures", failed_reads_throw},
    });
}

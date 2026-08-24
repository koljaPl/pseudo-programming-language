#include "test_support.hpp"

#include "pseudo/runtime.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

static_assert(std::is_same_v<
    decltype(tpp::runtime::vector_index(
        std::declval<std::vector<std::int64_t>&>(),
        std::int64_t{})),
    std::vector<std::int64_t>::reference>);
static_assert(std::is_same_v<
    decltype(tpp::runtime::vector_index(
        std::declval<const std::vector<std::int64_t>&>(),
        std::int64_t{})),
    std::vector<std::int64_t>::const_reference>);
static_assert(std::is_same_v<
    decltype(tpp::runtime::vector_index(
        std::declval<std::vector<bool>&>(),
        std::int64_t{})),
    std::vector<bool>::reference>);
static_assert(std::is_same_v<
    decltype(tpp::runtime::vector_index(
        std::declval<const std::vector<bool>&>(),
        std::int64_t{})),
    std::vector<bool>::const_reference>);

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

template <typename Exception>
void check_read_int_exception(
    std::string input_text,
    const std::string_view expected_message)
{
    std::istringstream input{std::move(input_text)};
    const ScopedCinBuffer redirected{input.rdbuf()};
    check_exception<Exception>(
        [] { (void)tpp::runtime::read_int(); },
        expected_message);
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

void vector_construction_checks_size_and_initializes_values()
{
    const auto empty = tpp::runtime::make_vector<std::int64_t>(0);
    TPP_CHECK(empty.empty());

    const auto integers = tpp::runtime::make_vector<std::int64_t>(3);
    TPP_CHECK_EQ(integers.size(), std::size_t{3});
    TPP_CHECK_EQ(integers[0], std::int64_t{0});
    TPP_CHECK_EQ(integers[2], std::int64_t{0});

    const auto booleans = tpp::runtime::make_vector<bool>(2);
    TPP_CHECK_EQ(booleans.size(), std::size_t{2});
    TPP_CHECK(!booleans[0]);
    TPP_CHECK(!booleans[1]);

    const auto characters = tpp::runtime::make_vector<char>(2);
    TPP_CHECK_EQ(characters[0], '\0');
    TPP_CHECK_EQ(characters[1], '\0');

    const auto strings = tpp::runtime::make_vector<std::string>(
        2,
        std::string{"seed"});
    TPP_CHECK_EQ(strings.size(), std::size_t{2});
    TPP_CHECK_EQ(strings[0], std::string{"seed"});
    TPP_CHECK_EQ(strings[1], std::string{"seed"});

    check_exception<std::length_error>(
        [] { (void)tpp::runtime::make_vector<int>(-1); },
        "vector size out of range");

    struct WideElement {
        std::array<char, 4096> storage{};
    };

    TPP_CHECK(std::cmp_less(
        std::vector<WideElement>{}.max_size(),
        std::numeric_limits<std::int64_t>::max()));
    check_exception<std::length_error>(
        [] {
            (void)tpp::runtime::make_vector<WideElement>(
                std::numeric_limits<std::int64_t>::max());
        },
        "vector size out of range");
}

void vector_indexing_is_checked_and_mutable()
{
    auto values = tpp::runtime::make_vector<std::int64_t>(3, 7);
    const auto& const_values = values;

    TPP_CHECK_EQ(tpp::runtime::vector_index(const_values, 0), 7);
    TPP_CHECK_EQ(tpp::runtime::vector_index(const_values, 2), 7);
    tpp::runtime::vector_index(values, 1) = 42;
    TPP_CHECK_EQ(values[1], 42);

    for (const auto index : {std::int64_t{-1}, std::int64_t{3}}) {
        check_exception<std::out_of_range>(
            [&] {
                (void)tpp::runtime::vector_index(const_values, index);
            },
            "vector index out of range");
        check_exception<std::out_of_range>(
            [&] {
                (void)tpp::runtime::vector_index(values, index);
            },
            "vector index out of range");
    }
}

void vector_bool_proxy_and_nested_vectors_work()
{
    auto bits = tpp::runtime::make_vector<bool>(3, false);
    tpp::runtime::vector_index(bits, 1) = true;
    const auto& const_bits = bits;
    TPP_CHECK(!tpp::runtime::vector_index(const_bits, 0));
    TPP_CHECK(tpp::runtime::vector_index(const_bits, 1));

    const auto row = tpp::runtime::make_vector<std::int64_t>(2, 7);
    auto matrix = tpp::runtime::make_vector<std::vector<std::int64_t>>(2, row);
    tpp::runtime::vector_index(
        tpp::runtime::vector_index(matrix, 1),
        0) = 9;

    TPP_CHECK_EQ(matrix[0][0], 7);
    TPP_CHECK_EQ(matrix[1][0], 9);
    TPP_CHECK_EQ(matrix[1][1], 7);

    const auto bit_row = tpp::runtime::make_vector<bool>(2, false);
    auto bit_matrix =
        tpp::runtime::make_vector<std::vector<bool>>(2, bit_row);
    tpp::runtime::vector_index(
        tpp::runtime::vector_index(bit_matrix, 1),
        0) = true;
    TPP_CHECK(!bit_matrix[0][0]);
    TPP_CHECK(bit_matrix[1][0]);
}

void integer_reads_accept_the_full_signed_64_bit_range()
{
    std::istringstream input{
        " \t\n0 42 -17 +23 00042 -0 "
        "9223372036854775807 -9223372036854775808"};
    const ScopedCinBuffer redirected{input.rdbuf()};

    TPP_CHECK_EQ(tpp::runtime::read_int(), std::int64_t{0});
    TPP_CHECK_EQ(tpp::runtime::read_int(), std::int64_t{42});
    TPP_CHECK_EQ(tpp::runtime::read_int(), std::int64_t{-17});
    TPP_CHECK_EQ(tpp::runtime::read_int(), std::int64_t{23});
    TPP_CHECK_EQ(tpp::runtime::read_int(), std::int64_t{42});
    TPP_CHECK_EQ(tpp::runtime::read_int(), std::int64_t{0});
    TPP_CHECK_EQ(
        tpp::runtime::read_int(),
        std::numeric_limits<std::int64_t>::max());
    TPP_CHECK_EQ(
        tpp::runtime::read_int(),
        std::numeric_limits<std::int64_t>::min());
}

void integer_reads_reject_malformed_whole_tokens()
{
    for (const std::string_view token : {
             "+",
             "-",
             "123abc",
             "1.0",
             "0x10",
             "++1",
             "--1",
             "+-1",
         }) {
        check_read_int_exception<std::runtime_error>(
            std::string{token},
            "failed to read int");
    }

    std::istringstream input{"123abc 17"};
    const ScopedCinBuffer redirected{input.rdbuf()};
    check_exception<std::runtime_error>(
        [] { (void)tpp::runtime::read_int(); },
        "failed to read int");
    TPP_CHECK_EQ(tpp::runtime::read_int(), std::int64_t{17});
}

void integer_reads_report_range_errors_without_overflow()
{
    for (const std::string_view token : {
             "9223372036854775808",
             "+9223372036854775808",
             "-9223372036854775809",
         }) {
        check_read_int_exception<std::out_of_range>(
            std::string{token},
            "integer input out of range");
    }

    check_read_int_exception<std::out_of_range>(
        std::string(1024, '9'),
        "integer input out of range");

    std::istringstream input{std::string(1024, '0') + " 8"};
    const ScopedCinBuffer redirected{input.rdbuf()};
    TPP_CHECK_EQ(tpp::runtime::read_int(), std::int64_t{0});
    TPP_CHECK_EQ(tpp::runtime::read_int(), std::int64_t{8});
}

void integer_reads_report_input_failure()
{
    check_read_int_exception<std::runtime_error>(
        "",
        "failed to read int");
    check_read_int_exception<std::runtime_error>(
        " \t\n",
        "failed to read int");

    std::istringstream input{"42"};
    const ScopedCinBuffer redirected{input.rdbuf()};
    std::cin.setstate(std::ios::badbit);
    check_exception<std::runtime_error>(
        [] { (void)tpp::runtime::read_int(); },
        "failed to read int");
}

void mixed_formatted_reads_share_the_stream_contract()
{
    std::istringstream input{" \t-12 word z +34"};
    const ScopedCinBuffer redirected{input.rdbuf()};

    TPP_CHECK_EQ(tpp::runtime::read_int(), std::int64_t{-12});
    TPP_CHECK_EQ(tpp::runtime::read_string(), std::string{"word"});
    TPP_CHECK_EQ(tpp::runtime::read_char(), 'z');
    TPP_CHECK_EQ(tpp::runtime::read_int(), std::int64_t{34});
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
        {"vector construction", vector_construction_checks_size_and_initializes_values},
        {"checked vector indexing", vector_indexing_is_checked_and_mutable},
        {"vector bool and nesting", vector_bool_proxy_and_nested_vectors_work},
        {"signed integer reads", integer_reads_accept_the_full_signed_64_bit_range},
        {"malformed integer reads", integer_reads_reject_malformed_whole_tokens},
        {"integer range errors", integer_reads_report_range_errors_without_overflow},
        {"integer input failures", integer_reads_report_input_failure},
        {"mixed formatted reads", mixed_formatted_reads_share_the_stream_contract},
        {"formatted reads", formatted_reads_skip_whitespace},
        {"read failures", failed_reads_throw},
    });
}

#pragma once

#include <cstddef>
#include <exception>
#include <initializer_list>
#include <iostream>
#include <sstream>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>

namespace tpp::test {

class Failure final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

inline std::string location_prefix(const std::source_location& location)
{
    return std::string(location.file_name()) + ':'
        + std::to_string(location.line()) + ": ";
}

inline void check(
    const bool condition,
    const std::string_view expression,
    const std::source_location location = std::source_location::current())
{
    if (!condition) {
        throw Failure(
            location_prefix(location) + "check failed: " + std::string(expression));
    }
}

template <typename Actual, typename Expected>
void check_equal(
    const Actual& actual,
    const Expected& expected,
    const std::string_view actual_expression,
    const std::string_view expected_expression,
    const std::source_location location = std::source_location::current())
{
    if (!(actual == expected)) {
        std::ostringstream details;
        if constexpr (requires { details << actual; details << expected; }) {
            details << " (actual: " << actual << ", expected: " << expected << ')';
        }

        throw Failure(
            location_prefix(location) + "expected " + std::string(actual_expression)
            + " == " + std::string(expected_expression) + details.str());
    }
}

inline void check_contains(
    const std::string_view text,
    const std::string_view expected,
    const std::source_location location = std::source_location::current())
{
    if (text.find(expected) == std::string_view::npos) {
        throw Failure(
            location_prefix(location) + "expected text to contain \""
            + std::string(expected) + "\"");
    }
}

struct Case {
    std::string_view name;
    void (*run)();
};

inline int run(const std::initializer_list<Case> cases)
{
    std::size_t failures = 0;

    for (const auto& test_case : cases) {
        try {
            test_case.run();
        } catch (const std::exception& exception) {
            ++failures;
            std::cerr << "[FAIL] " << test_case.name << '\n'
                      << "       " << exception.what() << '\n';
        } catch (...) {
            ++failures;
            std::cerr << "[FAIL] " << test_case.name
                      << "\n       unknown exception\n";
        }
    }

    if (failures != 0) {
        std::cerr << failures << " of " << cases.size() << " tests failed\n";
        return 1;
    }

    std::cout << cases.size() << " tests passed\n";
    return 0;
}

}

#define TPP_CHECK(expression) \
    ::tpp::test::check(static_cast<bool>(expression), #expression)

#define TPP_CHECK_EQ(actual, expected) \
    ::tpp::test::check_equal((actual), (expected), #actual, #expected)

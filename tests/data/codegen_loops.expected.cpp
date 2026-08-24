#include <cstdint>
#include <iostream>
#include <string>
#include <vector>
#include <pseudo/runtime.hpp>

int main()
{
    {
        const std::int64_t tpp_range_begin_1 = std::int64_t{0};
        const std::int64_t tpp_range_end_1 = std::int64_t{2};
        for (std::int64_t tpp_range_cursor_1 = tpp_range_begin_1; tpp_range_cursor_1 < tpp_range_end_1; ++tpp_range_cursor_1)
        {
            [[maybe_unused]] std::int64_t tpp_variable_1 = tpp_range_cursor_1;
            std::cout << std::boolalpha << tpp_variable_1 << '\n';
        }
    }
    {
        const std::int64_t tpp_range_begin_2 = std::int64_t{2};
        const std::int64_t tpp_range_end_2 = std::int64_t{3};
        bool tpp_range_active_2 = tpp_range_begin_2 <= tpp_range_end_2;
        for (std::int64_t tpp_range_cursor_2 = tpp_range_begin_2; tpp_range_active_2; tpp_range_active_2 = tpp_range_cursor_2 != tpp_range_end_2, tpp_range_cursor_2 += tpp_range_active_2 ? std::int64_t{1} : std::int64_t{0})
        {
            [[maybe_unused]] std::int64_t tpp_variable_2 = tpp_range_cursor_2;
            std::cout << std::boolalpha << tpp_variable_2 << '\n';
            continue;
        }
    }
    std::vector<bool> tpp_variable_3 = ([&]() -> std::vector<bool> { std::int64_t tpp_ordered_0 = std::int64_t{2}; bool tpp_ordered_1 = true; return tpp::runtime::make_vector<bool>(static_cast<std::int64_t&&>(tpp_ordered_0), static_cast<bool&&>(tpp_ordered_1)); }());
    {
        const std::vector<bool> tpp_iterable_4 = tpp_variable_3;
        for ([[maybe_unused]] bool tpp_variable_4 : tpp_iterable_4)
        {
            std::cout << std::boolalpha << tpp_variable_4 << '\n';
            break;
        }
    }
    {
        const std::string tpp_iterable_5 = std::string{"ok", 2};
        for ([[maybe_unused]] char tpp_variable_5 : tpp_iterable_5)
        {
            std::cout << std::boolalpha << tpp_variable_5 << '\n';
        }
    }
    return static_cast<int>(std::int64_t{0});
}

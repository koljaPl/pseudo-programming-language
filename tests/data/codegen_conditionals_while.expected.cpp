#include <cstdint>
#include <iostream>
#include <string>

int main()
{
    std::int64_t tpp_variable_1 = std::int64_t{0};
    while (([&]() -> bool { std::int64_t tpp_ordered_0 = tpp_variable_1; std::int64_t tpp_ordered_1 = std::int64_t{2}; return (static_cast<std::int64_t&&>(tpp_ordered_0) < static_cast<std::int64_t&&>(tpp_ordered_1)); }()))
    {
        if (([&]() -> bool { std::int64_t tpp_ordered_2 = tpp_variable_1; std::int64_t tpp_ordered_3 = std::int64_t{0}; return (static_cast<std::int64_t&&>(tpp_ordered_2) == static_cast<std::int64_t&&>(tpp_ordered_3)); }()))
        {
            std::cout << std::boolalpha << tpp_variable_1 << '\n';
        }
        else
        {
            std::cout << std::boolalpha << std::int64_t{9} << '\n';
        }
        tpp_variable_1 += std::int64_t{1};
    }
    return static_cast<int>(std::int64_t{0});
}

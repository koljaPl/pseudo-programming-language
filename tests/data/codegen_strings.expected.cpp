#include <cstdint>
#include <iostream>
#include <string>
#include <pseudo/runtime.hpp>

std::string tpp_function_0(std::string tpp_parameter_1, char tpp_parameter_2);
char tpp_function_3(std::string tpp_parameter_4);

std::string tpp_function_0(std::string tpp_parameter_1, char tpp_parameter_2)
{
    ([&]() -> void { auto&& tpp_ordered_0 = tpp_parameter_1; std::int64_t tpp_ordered_1 = std::int64_t{0}; auto&& tpp_ordered_2 = tpp::runtime::string_index(tpp_ordered_0, static_cast<std::int64_t&&>(tpp_ordered_1)); tpp_ordered_2 = tpp_parameter_2; }());
    tpp_parameter_1 += std::string{"!", 1};
    ([&]() -> void { std::string& tpp_ordered_3 = tpp_parameter_1; char tpp_ordered_4 = '?'; return tpp::runtime::string_push(tpp_ordered_3, static_cast<char&&>(tpp_ordered_4)); }());
    return tpp_parameter_1;
}

char tpp_function_3(std::string tpp_parameter_4)
{
    return ([&]() -> char { const std::string& tpp_ordered_5 = tpp_parameter_4; std::int64_t tpp_ordered_6 = std::int64_t{1}; return tpp::runtime::string_index(tpp_ordered_5, static_cast<std::int64_t&&>(tpp_ordered_6)); }());
}

int main()
{
    std::string tpp_variable_6 = std::string{"cat", 3};
    char tpp_variable_7 = 'B';
    std::cout << std::boolalpha << ([&]() -> std::string { std::string tpp_ordered_7 = tpp_variable_6; char tpp_ordered_8 = tpp_variable_7; return tpp_function_0(static_cast<std::string&&>(tpp_ordered_7), static_cast<char&&>(tpp_ordered_8)); }()) << '\n';
    std::cout << std::boolalpha << ([&]() -> std::string { std::string tpp_ordered_9 = std::string{"ab", 2}; std::string tpp_ordered_10 = std::string{"cd", 2}; return (static_cast<std::string&&>(tpp_ordered_9) + static_cast<std::string&&>(tpp_ordered_10)); }()) << '\n';
    std::cout << std::boolalpha << ([&]() -> bool { std::string tpp_ordered_11 = std::string{"same", 4}; std::string tpp_ordered_12 = std::string{"same", 4}; return (static_cast<std::string&&>(tpp_ordered_11) == static_cast<std::string&&>(tpp_ordered_12)); }()) << '\n';
    std::cout << std::boolalpha << ([&]() -> bool { std::string tpp_ordered_13 = std::string{"a", 1}; std::string tpp_ordered_14 = std::string{"b", 1}; return (static_cast<std::string&&>(tpp_ordered_13) != static_cast<std::string&&>(tpp_ordered_14)); }()) << '\n';
    std::cout << std::boolalpha << ([&]() -> bool { std::string tpp_ordered_15 = std::string{"a", 1}; std::string tpp_ordered_16 = std::string{"b", 1}; return (static_cast<std::string&&>(tpp_ordered_15) < static_cast<std::string&&>(tpp_ordered_16)); }()) << '\n';
    std::cout << std::boolalpha << ([&]() -> bool { std::string tpp_ordered_17 = std::string{"a", 1}; std::string tpp_ordered_18 = std::string{"a", 1}; return (static_cast<std::string&&>(tpp_ordered_17) <= static_cast<std::string&&>(tpp_ordered_18)); }()) << '\n';
    std::cout << std::boolalpha << ([&]() -> bool { std::string tpp_ordered_19 = std::string{"b", 1}; std::string tpp_ordered_20 = std::string{"a", 1}; return (static_cast<std::string&&>(tpp_ordered_19) > static_cast<std::string&&>(tpp_ordered_20)); }()) << '\n';
    std::cout << std::boolalpha << ([&]() -> bool { std::string tpp_ordered_21 = std::string{"b", 1}; std::string tpp_ordered_22 = std::string{"b", 1}; return (static_cast<std::string&&>(tpp_ordered_21) >= static_cast<std::string&&>(tpp_ordered_22)); }()) << '\n';
    std::cout << std::boolalpha << tpp::runtime::string_length(std::string{"A\000B", 3}) << '\n';
    std::cout << std::boolalpha << ([&]() -> std::string { std::string tpp_ordered_23 = std::string{"abcdef", 6}; std::int64_t tpp_ordered_24 = std::int64_t{1}; std::int64_t tpp_ordered_25 = std::int64_t{4}; return tpp::runtime::substring(static_cast<std::string&&>(tpp_ordered_23), static_cast<std::int64_t&&>(tpp_ordered_24), static_cast<std::int64_t&&>(tpp_ordered_25)); }()) << '\n';
    std::cout << std::boolalpha << tpp::runtime::string_length(tpp_variable_6) << '\n';
    ([&]() -> void { std::string& tpp_ordered_26 = tpp_variable_6; char tpp_ordered_27 = '!'; return tpp::runtime::string_push(tpp_ordered_26, static_cast<char&&>(tpp_ordered_27)); }());
    ([&]() -> void { auto&& tpp_ordered_28 = tpp_variable_6; std::int64_t tpp_ordered_29 = std::int64_t{0}; auto&& tpp_ordered_30 = tpp::runtime::string_index(tpp_ordered_28, static_cast<std::int64_t&&>(tpp_ordered_29)); tpp_ordered_30 = 'C'; }());
    std::cout << std::boolalpha << tpp_variable_6 << '\n';
    std::cout << std::boolalpha << tpp_function_3(std::string{"xy", 2}) << '\n';
    return static_cast<int>(std::int64_t{0});
}

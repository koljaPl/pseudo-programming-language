#include <cstdint>
#include <iostream>
#include <string>
#include <vector>
#include <pseudo/runtime.hpp>

std::vector<std::int64_t> tpp_function_0(std::vector<std::int64_t> tpp_parameter_1);
bool tpp_function_2(std::vector<bool> tpp_parameter_3);

std::vector<std::int64_t> tpp_function_0(std::vector<std::int64_t> tpp_parameter_1)
{
    return tpp_parameter_1;
}

bool tpp_function_2(std::vector<bool> tpp_parameter_3)
{
    return ([&]() -> bool { const std::vector<bool>& tpp_ordered_0 = tpp_parameter_3; std::int64_t tpp_ordered_1 = std::int64_t{1}; return tpp::runtime::vector_index(tpp_ordered_0, static_cast<std::int64_t&&>(tpp_ordered_1)); }());
}

int main()
{
    std::vector<std::int64_t> tpp_variable_5 = ([&]() -> std::vector<std::int64_t> { std::int64_t tpp_ordered_2 = std::int64_t{3}; std::int64_t tpp_ordered_3 = std::int64_t{7}; return tpp::runtime::make_vector<std::int64_t>(static_cast<std::int64_t&&>(tpp_ordered_2), static_cast<std::int64_t&&>(tpp_ordered_3)); }());
    std::vector<std::int64_t> tpp_variable_6 = std::vector<std::int64_t>{};
    tpp_variable_6 = tpp_variable_5;
    ([&]() -> void { auto&& tpp_ordered_4 = tpp_variable_5; std::int64_t tpp_ordered_5 = std::int64_t{1}; auto&& tpp_ordered_6 = tpp::runtime::vector_index(tpp_ordered_4, static_cast<std::int64_t&&>(tpp_ordered_5)); tpp_ordered_6 = std::int64_t{42}; }());
    ([&]() -> void { auto&& tpp_ordered_7 = tpp_variable_5; std::int64_t tpp_ordered_8 = std::int64_t{2}; auto&& tpp_ordered_9 = tpp::runtime::vector_index(tpp_ordered_7, static_cast<std::int64_t&&>(tpp_ordered_8)); tpp_ordered_9 += std::int64_t{1}; }());
    std::vector<bool> tpp_variable_7 = ([&]() -> std::vector<bool> { std::int64_t tpp_ordered_10 = std::int64_t{2}; bool tpp_ordered_11 = false; return tpp::runtime::make_vector<bool>(static_cast<std::int64_t&&>(tpp_ordered_10), static_cast<bool&&>(tpp_ordered_11)); }());
    ([&]() -> void { auto&& tpp_ordered_12 = tpp_variable_7; std::int64_t tpp_ordered_13 = std::int64_t{1}; auto&& tpp_ordered_14 = tpp::runtime::vector_index(tpp_ordered_12, static_cast<std::int64_t&&>(tpp_ordered_13)); tpp_ordered_14 = true; }());
    std::cout << std::boolalpha << ([&]() -> std::int64_t { const std::vector<std::int64_t>& tpp_ordered_15 = tpp_function_0(tpp_variable_5); std::int64_t tpp_ordered_16 = std::int64_t{0}; return tpp::runtime::vector_index(tpp_ordered_15, static_cast<std::int64_t&&>(tpp_ordered_16)); }()) << '\n';
    std::cout << std::boolalpha << ([&]() -> std::int64_t { const std::vector<std::int64_t>& tpp_ordered_17 = tpp_variable_6; std::int64_t tpp_ordered_18 = std::int64_t{1}; return tpp::runtime::vector_index(tpp_ordered_17, static_cast<std::int64_t&&>(tpp_ordered_18)); }()) << '\n';
    std::cout << std::boolalpha << ([&]() -> std::int64_t { const std::vector<std::int64_t>& tpp_ordered_19 = tpp_variable_5; std::int64_t tpp_ordered_20 = std::int64_t{1}; return tpp::runtime::vector_index(tpp_ordered_19, static_cast<std::int64_t&&>(tpp_ordered_20)); }()) << '\n';
    std::cout << std::boolalpha << ([&]() -> std::int64_t { const std::vector<std::int64_t>& tpp_ordered_21 = tpp_variable_5; std::int64_t tpp_ordered_22 = std::int64_t{2}; return tpp::runtime::vector_index(tpp_ordered_21, static_cast<std::int64_t&&>(tpp_ordered_22)); }()) << '\n';
    std::cout << std::boolalpha << tpp_function_2(tpp_variable_7) << '\n';
    return static_cast<int>(std::int64_t{0});
}

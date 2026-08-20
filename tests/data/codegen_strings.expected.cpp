#include <cstdint>
#include <iostream>
#include <string>
#include <pseudo/runtime.hpp>

std::string tpp_function_0(std::string tpp_parameter_1, char tpp_parameter_2);
char tpp_function_3(std::string tpp_parameter_4);

std::string tpp_function_0(std::string tpp_parameter_1, char tpp_parameter_2)
{
    tpp::runtime::string_index(tpp_parameter_1, std::int64_t{0}) = tpp_parameter_2;
    tpp_parameter_1 += std::string{"!", 1};
    tpp::runtime::string_push(tpp_parameter_1, '?');
    return tpp_parameter_1;
}

char tpp_function_3(std::string tpp_parameter_4)
{
    return tpp::runtime::string_index(tpp_parameter_4, std::int64_t{1});
}

int main()
{
    std::string tpp_variable_6 = std::string{"cat", 3};
    char tpp_variable_7 = 'B';
    std::cout << std::boolalpha << tpp_function_0(tpp_variable_6, tpp_variable_7) << '\n';
    std::cout << std::boolalpha << (std::string{"ab", 2} + std::string{"cd", 2}) << '\n';
    std::cout << std::boolalpha << (std::string{"same", 4} == std::string{"same", 4}) << '\n';
    std::cout << std::boolalpha << (std::string{"a", 1} != std::string{"b", 1}) << '\n';
    std::cout << std::boolalpha << (std::string{"a", 1} < std::string{"b", 1}) << '\n';
    std::cout << std::boolalpha << (std::string{"a", 1} <= std::string{"a", 1}) << '\n';
    std::cout << std::boolalpha << (std::string{"b", 1} > std::string{"a", 1}) << '\n';
    std::cout << std::boolalpha << (std::string{"b", 1} >= std::string{"b", 1}) << '\n';
    std::cout << std::boolalpha << tpp::runtime::string_length(std::string{"A\000B", 3}) << '\n';
    std::cout << std::boolalpha << tpp::runtime::substring(std::string{"abcdef", 6}, std::int64_t{1}, std::int64_t{4}) << '\n';
    std::cout << std::boolalpha << tpp::runtime::string_length(tpp_variable_6) << '\n';
    tpp::runtime::string_push(tpp_variable_6, '!');
    tpp::runtime::string_index(tpp_variable_6, std::int64_t{0}) = 'C';
    std::cout << std::boolalpha << tpp_variable_6 << '\n';
    std::cout << std::boolalpha << tpp_function_3(std::string{"xy", 2}) << '\n';
    return static_cast<int>(std::int64_t{0});
}

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
    return tpp::runtime::vector_index(tpp_parameter_3, std::int64_t{1});
}

int main()
{
    std::vector<std::int64_t> tpp_variable_5 = tpp::runtime::make_vector<std::int64_t>(std::int64_t{3}, std::int64_t{7});
    std::vector<std::int64_t> tpp_variable_6 = std::vector<std::int64_t>{};
    tpp_variable_6 = tpp_variable_5;
    tpp::runtime::vector_index(tpp_variable_5, std::int64_t{1}) = std::int64_t{42};
    tpp::runtime::vector_index(tpp_variable_5, std::int64_t{2}) += std::int64_t{1};
    std::vector<bool> tpp_variable_7 = tpp::runtime::make_vector<bool>(std::int64_t{2}, false);
    tpp::runtime::vector_index(tpp_variable_7, std::int64_t{1}) = true;
    std::cout << std::boolalpha << tpp::runtime::vector_index(tpp_function_0(tpp_variable_5), std::int64_t{0}) << '\n';
    std::cout << std::boolalpha << tpp::runtime::vector_index(tpp_variable_6, std::int64_t{1}) << '\n';
    std::cout << std::boolalpha << tpp::runtime::vector_index(tpp_variable_5, std::int64_t{1}) << '\n';
    std::cout << std::boolalpha << tpp::runtime::vector_index(tpp_variable_5, std::int64_t{2}) << '\n';
    std::cout << std::boolalpha << tpp_function_2(tpp_variable_7) << '\n';
    return static_cast<int>(std::int64_t{0});
}

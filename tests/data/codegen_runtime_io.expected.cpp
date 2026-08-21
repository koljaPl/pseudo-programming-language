#include <cstdint>
#include <iostream>
#include <string>
#include <pseudo/runtime.hpp>

int main()
{
    std::int64_t tpp_variable_1 = tpp::runtime::read_int();
    std::string tpp_variable_2 = tpp::runtime::read_string();
    char tpp_variable_3 = tpp::runtime::read_char();
    std::int64_t tpp_variable_4 = tpp::runtime::read_int();
    std::cout << std::boolalpha << tpp_variable_1 << '\n';
    std::cout << std::boolalpha << tpp_variable_2 << '\n';
    std::cout << std::boolalpha << tpp_variable_3 << '\n';
    std::cout << std::boolalpha << tpp_variable_4 << '\n';
    std::cout << std::boolalpha << true << '\n';
    std::cout << std::boolalpha << false << '\n';
    std::cout << std::boolalpha << std::string{"A\000B", 3} << '\n';
    return static_cast<int>(std::int64_t{0});
}

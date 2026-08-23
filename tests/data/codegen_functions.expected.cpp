#include <cstdint>
#include <iostream>
#include <string>

std::int64_t tpp_function_0(std::int64_t tpp_parameter_1, std::int64_t tpp_parameter_2);
void tpp_function_3(std::string tpp_parameter_4);
bool tpp_function_5(std::int64_t tpp_parameter_6);
bool tpp_function_7(std::int64_t tpp_parameter_8);
char tpp_function_9(char tpp_parameter_10);
std::string tpp_function_11(std::string tpp_parameter_12);

std::int64_t tpp_function_0(std::int64_t tpp_parameter_1, std::int64_t tpp_parameter_2)
{
    return ([&]() -> std::int64_t { std::int64_t tpp_ordered_0 = tpp_parameter_1; std::int64_t tpp_ordered_1 = tpp_parameter_2; return (static_cast<std::int64_t&&>(tpp_ordered_0) + static_cast<std::int64_t&&>(tpp_ordered_1)); }());
}

void tpp_function_3(std::string tpp_parameter_4)
{
    std::cout << std::boolalpha << tpp_parameter_4 << '\n';
}

bool tpp_function_5(std::int64_t tpp_parameter_6)
{
    return tpp_function_7(([&]() -> std::int64_t { std::int64_t tpp_ordered_2 = tpp_parameter_6; std::int64_t tpp_ordered_3 = std::int64_t{1}; return (static_cast<std::int64_t&&>(tpp_ordered_2) - static_cast<std::int64_t&&>(tpp_ordered_3)); }()));
}

bool tpp_function_7(std::int64_t tpp_parameter_8)
{
    return tpp_function_5(([&]() -> std::int64_t { std::int64_t tpp_ordered_4 = tpp_parameter_8; std::int64_t tpp_ordered_5 = std::int64_t{1}; return (static_cast<std::int64_t&&>(tpp_ordered_4) - static_cast<std::int64_t&&>(tpp_ordered_5)); }()));
}

char tpp_function_9(char tpp_parameter_10)
{
    return tpp_parameter_10;
}

std::string tpp_function_11(std::string tpp_parameter_12)
{
    return tpp_parameter_12;
}

int main()
{
    tpp_function_3(std::string{"sum", 3});
    std::cout << std::boolalpha << ([&]() -> std::int64_t { std::int64_t tpp_ordered_6 = std::int64_t{2}; std::int64_t tpp_ordered_7 = std::int64_t{3}; return tpp_function_0(static_cast<std::int64_t&&>(tpp_ordered_6), static_cast<std::int64_t&&>(tpp_ordered_7)); }()) << '\n';
    std::cout << std::boolalpha << tpp_function_5(std::int64_t{4}) << '\n';
    std::cout << std::boolalpha << tpp_function_9('x') << '\n';
    std::cout << std::boolalpha << tpp_function_11(std::string{"done", 4}) << '\n';
    return static_cast<int>(std::int64_t{0});
}

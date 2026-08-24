#include <cstdint>
#include <iostream>
#include <string>

int main()
{
    std::cout << std::boolalpha << std::string{"Hello", 5} << '\n';
    std::cout << std::boolalpha << std::int64_t{42} << '\n';
    return static_cast<int>(std::int64_t{0});
}

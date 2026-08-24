#include "cli.hpp"

#include <cstddef>
#include <iostream>
#include <string_view>
#include <vector>

int main(int argc, char* argv[]) {
    std::vector<std::string_view> arguments;

    if (argc > 1) {
        arguments.reserve(static_cast<std::size_t>(argc - 1));
    }

    for (int index = 1; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }

    return tpp::cli::run(arguments, std::cout, std::cerr);
}

#pragma once

#include <filesystem>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <variant>

namespace tpp::cli {

enum class Action {
    compile,
    help,
    version,
};

enum class OutputMode {
    none,
    ast,
    cpp,
};

struct Options {
    Action action = Action::compile;
    std::optional<std::filesystem::path> input;
    OutputMode output_mode = OutputMode::none;
};

struct Error {
    std::string message;
};

using ParseResult = std::variant<Options, Error>;

[[nodiscard]] ParseResult parse_args(std::span<const std::string_view> args);

[[nodiscard]] int run(
    std::span<const std::string_view> args,
    std::ostream& out,
    std::ostream& err);

}

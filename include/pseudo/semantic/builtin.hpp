#pragma once

#include <optional>
#include <string_view>

namespace tpp {

enum class BuiltinFunctionKind {
    print,
    read_int,
    read_string,
    read_char,
    len,
    substring,
};

[[nodiscard]] std::optional<BuiltinFunctionKind> find_builtin_function(
    std::string_view name) noexcept;

}

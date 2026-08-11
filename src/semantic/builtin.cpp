#include "pseudo/semantic/builtin.hpp"

namespace tpp {

std::optional<BuiltinFunctionKind> find_builtin_function(
    const std::string_view name) noexcept {
    if (name == "print") {
        return BuiltinFunctionKind::print;
    }
    if (name == "read_int") {
        return BuiltinFunctionKind::read_int;
    }
    if (name == "read_string") {
        return BuiltinFunctionKind::read_string;
    }
    if (name == "read_char") {
        return BuiltinFunctionKind::read_char;
    }
    if (name == "len") {
        return BuiltinFunctionKind::len;
    }
    if (name == "substring") {
        return BuiltinFunctionKind::substring;
    }

    return std::nullopt;
}

}

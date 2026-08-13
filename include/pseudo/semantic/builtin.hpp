#pragma once

#include "pseudo/semantic/type_context.hpp"

#include <optional>
#include <span>
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

enum class BuiltinParameterKind {
    integer,
    character,
    string,
    printable_scalar,
};

struct BuiltinFunctionSignature {
    PrimitiveTypeKind return_type;
    std::span<const BuiltinParameterKind> parameter_types;
};

[[nodiscard]] std::optional<BuiltinFunctionKind> find_builtin_function(
    std::string_view name) noexcept;

[[nodiscard]] BuiltinFunctionSignature builtin_function_signature(
    BuiltinFunctionKind builtin) noexcept;

[[nodiscard]] bool builtin_parameter_accepts(
    BuiltinParameterKind parameter,
    PrimitiveTypeKind argument) noexcept;

}

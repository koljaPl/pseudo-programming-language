#include "pseudo/semantic/builtin.hpp"

#include <array>

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

BuiltinFunctionSignature builtin_function_signature(
    const BuiltinFunctionKind builtin) noexcept {
    static constexpr std::array<BuiltinParameterKind, 0> no_parameters{};
    static constexpr std::array print_parameters{
        BuiltinParameterKind::printable_scalar,
    };
    static constexpr std::array len_parameters{
        BuiltinParameterKind::string,
    };
    static constexpr std::array substring_parameters{
        BuiltinParameterKind::string,
        BuiltinParameterKind::integer,
        BuiltinParameterKind::integer,
    };

    switch (builtin) {
    case BuiltinFunctionKind::print:
        return {
            .return_type = PrimitiveTypeKind::void_type,
            .parameter_types = print_parameters,
        };
    case BuiltinFunctionKind::read_int:
        return {
            .return_type = PrimitiveTypeKind::integer,
            .parameter_types = no_parameters,
        };
    case BuiltinFunctionKind::read_string:
        return {
            .return_type = PrimitiveTypeKind::string,
            .parameter_types = no_parameters,
        };
    case BuiltinFunctionKind::read_char:
        return {
            .return_type = PrimitiveTypeKind::character,
            .parameter_types = no_parameters,
        };
    case BuiltinFunctionKind::len:
        return {
            .return_type = PrimitiveTypeKind::integer,
            .parameter_types = len_parameters,
        };
    case BuiltinFunctionKind::substring:
        return {
            .return_type = PrimitiveTypeKind::string,
            .parameter_types = substring_parameters,
        };
    }

    return {
        .return_type = PrimitiveTypeKind::void_type,
        .parameter_types = no_parameters,
    };
}

bool builtin_parameter_accepts(
    const BuiltinParameterKind parameter,
    const PrimitiveTypeKind argument) noexcept {
    switch (parameter) {
    case BuiltinParameterKind::integer:
        return argument == PrimitiveTypeKind::integer;
    case BuiltinParameterKind::character:
        return argument == PrimitiveTypeKind::character;
    case BuiltinParameterKind::string:
        return argument == PrimitiveTypeKind::string;
    case BuiltinParameterKind::printable_scalar:
        return argument == PrimitiveTypeKind::integer
            || argument == PrimitiveTypeKind::boolean
            || argument == PrimitiveTypeKind::character
            || argument == PrimitiveTypeKind::string;
    }

    return false;
}

}

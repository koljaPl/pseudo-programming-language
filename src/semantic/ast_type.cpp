#include "pseudo/semantic/ast_type.hpp"

#include <type_traits>
#include <variant>

namespace tpp {
namespace {

std::optional<TypeId> primitive_type_id(
    TypeContext& types,
    const ScalarTypeKind kind) noexcept {
    switch (kind) {
    case ScalarTypeKind::integer:
        return types.integer_type();
    case ScalarTypeKind::boolean:
        return types.boolean_type();
    case ScalarTypeKind::character:
        return types.character_type();
    case ScalarTypeKind::string:
        return types.string_type();
    }

    return std::nullopt;
}

}

std::optional<TypeId> type_id_for(
    TypeContext& types,
    const ValueType& type) {
    return std::visit(
        [&types](const auto& node) -> std::optional<TypeId> {
            using Node = std::decay_t<decltype(node)>;

            if constexpr (std::is_same_v<Node, ScalarTypeKind>) {
                return primitive_type_id(types, node);
            } else {
                if (node.element_type == nullptr) {
                    return std::nullopt;
                }

                const auto element_type =
                    type_id_for(types, *node.element_type);
                if (!element_type.has_value()) {
                    return std::nullopt;
                }

                return types.vector_type(*element_type);
            }
        },
        type.node);
}

std::optional<TypeId> type_id_for(
    TypeContext& types,
    const ReturnType& type) {
    return std::visit(
        [&types](const auto& node) -> std::optional<TypeId> {
            using Node = std::decay_t<decltype(node)>;

            if constexpr (std::is_same_v<Node, VoidType>) {
                return types.void_type();
            } else {
                return type_id_for(types, node);
            }
        },
        type.node);
}

}

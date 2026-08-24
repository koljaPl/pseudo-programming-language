#pragma once

#include "pseudo/ast/type.hpp"
#include "pseudo/semantic/type_context.hpp"

#include <optional>

namespace tpp {

[[nodiscard]] std::optional<TypeId> type_id_for(
    TypeContext& types,
    const ValueType& type);

[[nodiscard]] std::optional<TypeId> type_id_for(
    TypeContext& types,
    const ReturnType& type);

}

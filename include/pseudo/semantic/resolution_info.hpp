#pragma once

#include "pseudo/ast/program.hpp"
#include "pseudo/semantic/builtin.hpp"
#include "pseudo/semantic/symbol_table.hpp"

#include <optional>
#include <unordered_map>
#include <variant>

namespace tpp {

class NameResolver;

using ResolutionTarget = std::variant<SymbolId, BuiltinFunctionKind>;

// These mappings borrow AST node addresses. They are valid while the resolved
// Program remains alive, unmoved, and structurally unchanged.
class ResolutionInfo {
public:
    [[nodiscard]] std::optional<ResolutionTarget> resolution_for(
        const IdentifierExpression& reference) const;
    [[nodiscard]] std::optional<ResolutionTarget> resolution_for(
        const AssignmentTarget& reference) const;

    [[nodiscard]] bool empty() const noexcept;

private:
    friend class NameResolver;

    void record(
        const IdentifierExpression& reference,
        ResolutionTarget target);
    void record(
        const AssignmentTarget& reference,
        ResolutionTarget target);

    std::unordered_map<const IdentifierExpression*, ResolutionTarget>
        identifiers_;
    std::unordered_map<const AssignmentTarget*, ResolutionTarget>
        assignment_targets_;
};

}

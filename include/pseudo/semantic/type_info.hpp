#pragma once

#include "pseudo/ast/program.hpp"
#include "pseudo/semantic/member_kind.hpp"
#include "pseudo/semantic/symbol_table.hpp"
#include "pseudo/semantic/type_context.hpp"

#include <cstddef>
#include <optional>
#include <unordered_map>

namespace tpp {

class TypeChecker;

// AST-address mappings remain valid while the checked Program is alive,
// unmoved, and structurally unchanged. Inferred symbol types are local to the
// SymbolTable used for the same checking pass. Callable expressions have no
// TypeId because functions are not first-class values; successful calls,
// including void calls, do have a recorded result type.
class TypeInfo {
public:
    [[nodiscard]] std::optional<TypeId> type_of(
        const Expression& expression) const;
    [[nodiscard]] std::optional<TypeId> type_of(
        const AssignmentTarget& target) const;
    [[nodiscard]] std::optional<TypeId> inferred_type(
        SymbolId symbol) const;
    [[nodiscard]] std::optional<MemberKind> member_for(
        const MemberAccessExpression& member) const;

    [[nodiscard]] bool empty() const noexcept;

private:
    friend class TypeChecker;

    void record(const Expression& expression, TypeId type);
    void record(const AssignmentTarget& target, TypeId type);
    void record(SymbolId symbol, TypeId type);
    void record(const MemberAccessExpression& member, MemberKind kind);

    std::unordered_map<const Expression*, TypeId> expressions_;
    std::unordered_map<const AssignmentTarget*, TypeId> assignment_targets_;
    std::unordered_map<std::size_t, TypeId> inferred_symbols_;
    std::unordered_map<const MemberAccessExpression*, MemberKind> members_;
};

}

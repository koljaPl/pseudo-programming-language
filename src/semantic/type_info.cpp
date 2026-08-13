#include "pseudo/semantic/type_info.hpp"

namespace tpp {

std::optional<TypeId> TypeInfo::type_of(
    const Expression& expression) const {
    const auto found = expressions_.find(&expression);
    return found == expressions_.end()
        ? std::nullopt
        : std::optional<TypeId>{found->second};
}

std::optional<TypeId> TypeInfo::type_of(
    const AssignmentTarget& target) const {
    const auto found = assignment_targets_.find(&target);
    return found == assignment_targets_.end()
        ? std::nullopt
        : std::optional<TypeId>{found->second};
}

std::optional<TypeId> TypeInfo::inferred_type(const SymbolId symbol) const {
    const auto found = inferred_symbols_.find(symbol.value);
    return found == inferred_symbols_.end()
        ? std::nullopt
        : std::optional<TypeId>{found->second};
}

std::optional<MemberKind> TypeInfo::member_for(
    const MemberAccessExpression& member) const {
    const auto found = members_.find(&member);
    return found == members_.end()
        ? std::nullopt
        : std::optional<MemberKind>{found->second};
}

bool TypeInfo::empty() const noexcept {
    return expressions_.empty()
        && assignment_targets_.empty()
        && inferred_symbols_.empty()
        && members_.empty();
}

void TypeInfo::record(const Expression& expression, const TypeId type) {
    expressions_.insert_or_assign(&expression, type);
}

void TypeInfo::record(const AssignmentTarget& target, const TypeId type) {
    assignment_targets_.insert_or_assign(&target, type);
}

void TypeInfo::record(const SymbolId symbol, const TypeId type) {
    inferred_symbols_.insert_or_assign(symbol.value, type);
}

void TypeInfo::record(
    const MemberAccessExpression& member,
    const MemberKind kind) {
    members_.insert_or_assign(&member, kind);
}

}

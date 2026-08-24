#include "pseudo/semantic/declaration_info.hpp"

namespace tpp {
namespace {

template <typename Node, typename Id>
std::optional<Id> find_mapping(
    const std::unordered_map<const Node*, Id>& mappings,
    const Node& node) {
    const auto found = mappings.find(&node);
    if (found == mappings.end()) {
        return std::nullopt;
    }

    return found->second;
}

}

std::optional<SymbolId> DeclarationInfo::symbol_for(
    const FunctionDeclaration& declaration) const {
    return find_mapping(functions_, declaration);
}

std::optional<SymbolId> DeclarationInfo::symbol_for(
    const VariableDeclaration& declaration) const {
    return find_mapping(variables_, declaration);
}

std::optional<SymbolId> DeclarationInfo::symbol_for(
    const Parameter& declaration) const {
    return find_mapping(parameters_, declaration);
}

std::optional<SymbolId> DeclarationInfo::symbol_for(
    const ForRangeStatement& declaration) const {
    return find_mapping(range_bindings_, declaration);
}

std::optional<SymbolId> DeclarationInfo::symbol_for(
    const ForEachStatement& declaration) const {
    return find_mapping(each_bindings_, declaration);
}

std::optional<ScopeId> DeclarationInfo::scope_for(
    const Block& block) const {
    return find_mapping(blocks_, block);
}

bool DeclarationInfo::empty() const noexcept {
    return functions_.empty()
        && variables_.empty()
        && parameters_.empty()
        && range_bindings_.empty()
        && each_bindings_.empty()
        && blocks_.empty();
}

void DeclarationInfo::record(
    const FunctionDeclaration& declaration,
    const SymbolId symbol) {
    functions_.emplace(&declaration, symbol);
}

void DeclarationInfo::record(
    const VariableDeclaration& declaration,
    const SymbolId symbol) {
    variables_.emplace(&declaration, symbol);
}

void DeclarationInfo::record(
    const Parameter& declaration,
    const SymbolId symbol) {
    parameters_.emplace(&declaration, symbol);
}

void DeclarationInfo::record(
    const ForRangeStatement& declaration,
    const SymbolId symbol) {
    range_bindings_.emplace(&declaration, symbol);
}

void DeclarationInfo::record(
    const ForEachStatement& declaration,
    const SymbolId symbol) {
    each_bindings_.emplace(&declaration, symbol);
}

void DeclarationInfo::record(const Block& block, const ScopeId scope) {
    blocks_.emplace(&block, scope);
}

}

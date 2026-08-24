#pragma once

#include "pseudo/ast/program.hpp"
#include "pseudo/semantic/symbol_table.hpp"

#include <optional>
#include <unordered_map>

namespace tpp {

class DeclarationCollector;

// These mappings borrow AST node addresses. They are valid while the collected
// Program remains alive, unmoved, and structurally unchanged.
class DeclarationInfo {
public:
    [[nodiscard]] std::optional<SymbolId> symbol_for(
        const FunctionDeclaration& declaration) const;
    [[nodiscard]] std::optional<SymbolId> symbol_for(
        const VariableDeclaration& declaration) const;
    [[nodiscard]] std::optional<SymbolId> symbol_for(
        const Parameter& declaration) const;
    [[nodiscard]] std::optional<SymbolId> symbol_for(
        const ForRangeStatement& declaration) const;
    [[nodiscard]] std::optional<SymbolId> symbol_for(
        const ForEachStatement& declaration) const;

    [[nodiscard]] std::optional<ScopeId> scope_for(
        const Block& block) const;

    [[nodiscard]] bool empty() const noexcept;

private:
    friend class DeclarationCollector;

    void record(const FunctionDeclaration& declaration, SymbolId symbol);
    void record(const VariableDeclaration& declaration, SymbolId symbol);
    void record(const Parameter& declaration, SymbolId symbol);
    void record(const ForRangeStatement& declaration, SymbolId symbol);
    void record(const ForEachStatement& declaration, SymbolId symbol);
    void record(const Block& block, ScopeId scope);

    std::unordered_map<const FunctionDeclaration*, SymbolId> functions_;
    std::unordered_map<const VariableDeclaration*, SymbolId> variables_;
    std::unordered_map<const Parameter*, SymbolId> parameters_;
    std::unordered_map<const ForRangeStatement*, SymbolId> range_bindings_;
    std::unordered_map<const ForEachStatement*, SymbolId> each_bindings_;
    std::unordered_map<const Block*, ScopeId> blocks_;
};

}

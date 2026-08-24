#pragma once

#include "pseudo/ast/program.hpp"
#include "pseudo/semantic/declaration_info.hpp"
#include "pseudo/semantic/resolution_info.hpp"
#include "pseudo/semantic/symbol_table.hpp"

#include <optional>
#include <string_view>
#include <vector>

namespace tpp {

class DiagnosticEngine;

// Resolution is a one-shot pass over a Program and declaration state produced
// for that exact Program by DeclarationCollector.
class NameResolver {
public:
    NameResolver(
        const SymbolTable& symbols,
        const DeclarationInfo& declarations,
        ResolutionInfo& resolutions,
        DiagnosticEngine& diagnostics);

    [[nodiscard]] bool resolve(const Program& program);

private:
    void resolve_top_level(const TopLevelDeclaration& declaration);
    void resolve_function(const FunctionDeclaration& declaration);
    void resolve_block(
        const Block& block,
        ScopeId scope,
        ScopeId function_scope);
    void resolve_block_item(
        const BlockItem& item,
        ScopeId scope,
        ScopeId function_scope);
    void resolve_statement(
        const Statement& statement,
        ScopeId scope,
        ScopeId function_scope);
    void resolve_variable(
        const VariableDeclaration& declaration,
        ScopeId scope,
        std::optional<ScopeId> function_scope);
    void resolve_assignment_target(
        const AssignmentTarget& target,
        ScopeId scope,
        std::optional<ScopeId> function_scope);
    void resolve_expression(
        const Expression& expression,
        ScopeId scope,
        std::optional<ScopeId> function_scope);
    void resolve_required_expression(
        const ExpressionPtr& expression,
        SourceSpan owner_span,
        ScopeId scope,
        std::optional<ScopeId> function_scope);
    void resolve_child_block(
        const BlockPtr& block,
        SourceSpan owner_span,
        ScopeId function_scope);
    void resolve_for_range(
        const ForRangeStatement& statement,
        SourceSpan owner_span,
        ScopeId scope,
        ScopeId function_scope);
    void resolve_for_each(
        const ForEachStatement& statement,
        SourceSpan owner_span,
        ScopeId scope,
        ScopeId function_scope);

    [[nodiscard]] std::optional<ResolutionTarget> resolve_name(
        std::string_view name,
        SourceSpan span,
        ScopeId scope,
        std::optional<ScopeId> function_scope);
    [[nodiscard]] bool is_visible(
        SymbolId symbol,
        ScopeId declaring_scope) const;
    void activate(SymbolId symbol);

    const SymbolTable& symbols_;
    const DeclarationInfo& declarations_;
    ResolutionInfo& resolutions_;
    DiagnosticEngine& diagnostics_;
    std::vector<bool> active_local_variables_;
};

}

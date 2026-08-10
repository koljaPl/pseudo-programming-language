#pragma once

#include "pseudo/ast/program.hpp"
#include "pseudo/semantic/declaration_info.hpp"
#include "pseudo/semantic/symbol_table.hpp"

#include <optional>

namespace tpp {

class DiagnosticEngine;
class TypeContext;

// Collection is a one-shot pass over a stable Program and fresh semantic
// state. It deliberately does not inspect expressions.
class DeclarationCollector {
public:
    DeclarationCollector(
        TypeContext& types,
        SymbolTable& symbols,
        DeclarationInfo& declarations,
        DiagnosticEngine& diagnostics);

    [[nodiscard]] bool collect(const Program& program);

private:
    void collect_top_level(
        const TopLevelDeclaration& declaration,
        ScopeId scope);
    void collect_function(
        const FunctionDeclaration& declaration,
        ScopeId enclosing_scope);
    void collect_variable(
        const VariableDeclaration& declaration,
        ScopeId scope);
    void collect_block(const Block& block, ScopeId scope);
    void collect_block_item(const BlockItem& item, ScopeId scope);
    void collect_statement(const Statement& statement, ScopeId scope);
    void collect_child_block(
        const BlockPtr& block,
        ScopeId parent,
        SourceSpan owner_span,
        const char* missing_body_message);
    void collect_for_range(
        const ForRangeStatement& statement,
        SourceSpan span,
        ScopeId enclosing_scope);
    void collect_for_each(
        const ForEachStatement& statement,
        SourceSpan span,
        ScopeId enclosing_scope);

    [[nodiscard]] std::optional<SymbolId> insert_symbol(
        ScopeId scope,
        Symbol symbol);
    void report_invalid_type(SourceSpan span);

    TypeContext& types_;
    SymbolTable& symbols_;
    DeclarationInfo& declarations_;
    DiagnosticEngine& diagnostics_;
};

}

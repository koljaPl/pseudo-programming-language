#pragma once

#include "pseudo/ast/program.hpp"
#include "pseudo/semantic/declaration_info.hpp"
#include "pseudo/semantic/resolution_info.hpp"
#include "pseudo/semantic/symbol_table.hpp"
#include "pseudo/semantic/type_context.hpp"
#include "pseudo/semantic/type_info.hpp"

#include <optional>
#include <string>

namespace tpp {

class DiagnosticEngine;

// Type checking is a one-shot pass over a Program successfully processed by
// DeclarationCollector and NameResolver.
class TypeChecker {
public:
    TypeChecker(
        TypeContext& types,
        const SymbolTable& symbols,
        const DeclarationInfo& declarations,
        const ResolutionInfo& resolutions,
        TypeInfo& type_info,
        DiagnosticEngine& diagnostics);

    [[nodiscard]] bool check(const Program& program);

private:
    struct ExpressionResult;
    struct Callable;

    void check_top_level(const TopLevelDeclaration& declaration);
    void check_function(const FunctionDeclaration& declaration);
    void check_block(const Block& block);
    void check_block_item(const BlockItem& item);
    void check_statement(const Statement& statement);
    void check_variable(
        const VariableDeclaration& declaration,
        bool is_global);
    void check_assignment(
        const AssignmentStatement& statement,
        SourceSpan span);
    void check_for_range(
        const ForRangeStatement& statement,
        SourceSpan span);
    void check_for_each(
        const ForEachStatement& statement,
        SourceSpan span);

    [[nodiscard]] ExpressionResult check_expression(
        const Expression& expression);
    [[nodiscard]] ExpressionResult check_expression(
        const Expression& expression,
        bool allow_minimum_integer_magnitude);
    [[nodiscard]] ExpressionResult check_required_expression(
        const ExpressionPtr& expression,
        SourceSpan owner_span);
    [[nodiscard]] ExpressionResult check_identifier(
        const Expression& expression,
        const IdentifierExpression& identifier);
    [[nodiscard]] ExpressionResult check_call(
        const Expression& expression,
        const CallExpression& call);
    [[nodiscard]] ExpressionResult check_member(
        const Expression& expression,
        const MemberAccessExpression& member);
    [[nodiscard]] std::optional<TypeId> check_assignment_target(
        const AssignmentTarget& target);
    [[nodiscard]] bool is_mutable_string_receiver(
        const Expression& expression) const;

    [[nodiscard]] ExpressionResult check_callable(
        const Expression& expression,
        const CallExpression& call,
        const Callable& callable);
    void check_condition(
        const ExpressionPtr& condition,
        SourceSpan owner_span);
    void check_child_block(const BlockPtr& block, SourceSpan owner_span);
    void report_type_mismatch(
        SourceSpan span,
        std::string context,
        TypeId expected,
        TypeId actual);

    [[nodiscard]] std::optional<TypeId> type_of_symbol(SymbolId symbol) const;
    [[nodiscard]] std::optional<PrimitiveTypeKind> primitive_kind(
        TypeId type) const;
    [[nodiscard]] std::optional<TypeId> primitive_type(
        PrimitiveTypeKind kind) const noexcept;
    [[nodiscard]] std::string type_name(TypeId type) const;

    TypeContext& types_;
    const SymbolTable& symbols_;
    const DeclarationInfo& declarations_;
    const ResolutionInfo& resolutions_;
    TypeInfo& type_info_;
    DiagnosticEngine& diagnostics_;
    std::optional<TypeId> current_return_type_;
};

}

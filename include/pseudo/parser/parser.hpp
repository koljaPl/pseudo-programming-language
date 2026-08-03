#pragma once

#include "pseudo/ast/program.hpp"
#include "pseudo/lexer/token.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>

namespace tpp {

class DiagnosticEngine;
class SourceManager;

class Parser {
public:
    Parser(
        std::span<const Token> tokens,
        const SourceManager& sources,
        DiagnosticEngine& diagnostics);

    [[nodiscard]] Program parse_program();

private:
    using ParsedDeclaration =
        std::variant<FunctionDeclaration, VariableDeclaration>;

    [[nodiscard]] std::optional<TopLevelDeclaration>
    parse_top_level_declaration();
    [[nodiscard]] std::optional<ParsedDeclaration>
    parse_typed_declaration();
    [[nodiscard]] std::optional<FunctionDeclaration> parse_function(
        ReturnType return_type,
        const Token& name);
    [[nodiscard]] std::optional<VariableDeclaration> parse_variable(
        ValueType type,
        const Token& name);
    [[nodiscard]] std::optional<Parameter> parse_parameter();

    [[nodiscard]] std::optional<ValueType> parse_value_type();
    [[nodiscard]] std::optional<ReturnType> parse_return_type();
    [[nodiscard]] bool looks_like_value_declaration() const noexcept;

    [[nodiscard]] BlockPtr parse_block();
    [[nodiscard]] std::optional<BlockItem> parse_block_item();
    [[nodiscard]] std::optional<Statement> parse_statement();
    [[nodiscard]] std::optional<Statement> parse_if_statement();
    [[nodiscard]] std::optional<Statement> parse_while_statement();
    [[nodiscard]] std::optional<Statement> parse_for_statement();
    [[nodiscard]] std::optional<Statement> parse_return_statement();
    [[nodiscard]] Statement parse_break_statement();
    [[nodiscard]] Statement parse_continue_statement();
    [[nodiscard]] std::optional<Statement> parse_block_statement();
    [[nodiscard]] std::optional<Statement>
    parse_expression_or_assignment_statement();

    [[nodiscard]] ExpressionPtr parse_expression();
    [[nodiscard]] std::optional<AssignmentTarget>
    take_assignment_target(ExpressionPtr expression);

    [[nodiscard]] bool at_end() const noexcept;
    [[nodiscard]] bool check(TokenKind kind) const noexcept;
    [[nodiscard]] bool match(TokenKind kind) noexcept;
    [[nodiscard]] const Token& current() const noexcept;
    [[nodiscard]] const Token& previous() const noexcept;
    const Token& advance() noexcept;

    [[nodiscard]] SourceSpan insertion_span() const noexcept;
    [[nodiscard]] SourceSpan finish_statement(std::string_view message);
    void report(SourceSpan span, std::string message);
    void report_at_current(std::string message);

    void synchronize_top_level(std::size_t construct_begin) noexcept;
    void synchronize_statement(std::size_t construct_begin) noexcept;
    void skip_malformed_function() noexcept;

    std::span<const Token> tokens_;
    const SourceManager& sources_;
    DiagnosticEngine& diagnostics_;
    std::size_t index_ = 0;
};

}

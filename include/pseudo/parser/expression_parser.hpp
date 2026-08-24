#pragma once

#include "pseudo/ast/expression.hpp"
#include "pseudo/lexer/token.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace tpp {

class DiagnosticEngine;
class Parser;
class SourceManager;

class ExpressionParser {
public:
    ExpressionParser(
        std::span<const Token> tokens,
        const SourceManager& sources,
        DiagnosticEngine& diagnostics);

    // Parses one complete expression followed by EOF.
    [[nodiscard]] ExpressionPtr parse();

private:
    friend class Parser;

    struct ValidatedTokenStream {
    };

    ExpressionParser(
        std::span<const Token> tokens,
        const SourceManager& sources,
        DiagnosticEngine& diagnostics,
        ValidatedTokenStream) noexcept;

    [[nodiscard]] ExpressionPtr parse_prefix();
    [[nodiscard]] std::size_t consumed_token_count() const noexcept;

    [[nodiscard]] ExpressionPtr parse_expression();
    [[nodiscard]] ExpressionPtr parse_logical_or();
    [[nodiscard]] ExpressionPtr parse_logical_and();
    [[nodiscard]] ExpressionPtr parse_equality();
    [[nodiscard]] ExpressionPtr parse_comparison();
    [[nodiscard]] ExpressionPtr parse_additive();
    [[nodiscard]] ExpressionPtr parse_multiplicative();
    [[nodiscard]] ExpressionPtr parse_unary();
    [[nodiscard]] ExpressionPtr parse_postfix();
    [[nodiscard]] ExpressionPtr parse_primary();
    [[nodiscard]] ExpressionPtr parse_parenthesized();
    [[nodiscard]] ExpressionPtr parse_vector_construction();

    [[nodiscard]] std::optional<ValueType> parse_value_type();
    [[nodiscard]] std::optional<SourceSpan> parse_argument_list(
        std::vector<ExpressionPtr>& arguments);

    [[nodiscard]] bool at_end() const noexcept;
    [[nodiscard]] bool check(TokenKind kind) const noexcept;
    [[nodiscard]] bool match(TokenKind kind) noexcept;
    [[nodiscard]] const Token& current() const noexcept;
    [[nodiscard]] const Token& previous() const noexcept;
    const Token& advance() noexcept;

    [[nodiscard]] SourceSpan insertion_span() const noexcept;
    void report(SourceSpan span, std::string message);
    void report_at_current(std::string message);
    void synchronize() noexcept;

    std::span<const Token> tokens_;
    const SourceManager& sources_;
    DiagnosticEngine& diagnostics_;
    std::size_t index_ = 0;
    bool failed_ = false;
};

}

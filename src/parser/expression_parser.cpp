#include "pseudo/parser/expression_parser.hpp"

#include "pseudo/diagnostics/diagnostic_engine.hpp"
#include "pseudo/source/source_manager.hpp"

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

namespace tpp {
namespace {

SourceSpan joined_span(SourceSpan first, SourceSpan last) noexcept {
    return SourceSpan{
        .source = first.source,
        .begin = first.begin,
        .end = last.end,
    };
}

template <typename Node>
ExpressionPtr make_expression(SourceSpan span, Node node) {
    return std::make_unique<Expression>(Expression{
        .span = span,
        .node = std::move(node),
    });
}

ExpressionPtr make_binary(
    ExpressionPtr left,
    BinaryOperator operator_kind,
    ExpressionPtr right) {
    const auto span = joined_span(left->span, right->span);
    return make_expression(
        span,
        BinaryExpression{
            .operator_kind = operator_kind,
            .left = std::move(left),
            .right = std::move(right),
        });
}

std::optional<UnaryOperator> unary_operator(TokenKind kind) noexcept {
    switch (kind) {
    case TokenKind::plus:
        return UnaryOperator::plus;
    case TokenKind::minus:
        return UnaryOperator::minus;
    case TokenKind::logical_not:
        return UnaryOperator::logical_not;
    default:
        return std::nullopt;
    }
}

std::optional<BinaryOperator> binary_operator(TokenKind kind) noexcept {
    switch (kind) {
    case TokenKind::logical_or:
        return BinaryOperator::logical_or;
    case TokenKind::logical_and:
        return BinaryOperator::logical_and;
    case TokenKind::equal:
        return BinaryOperator::equal;
    case TokenKind::not_equal:
        return BinaryOperator::not_equal;
    case TokenKind::less:
        return BinaryOperator::less;
    case TokenKind::less_equal:
        return BinaryOperator::less_equal;
    case TokenKind::greater:
        return BinaryOperator::greater;
    case TokenKind::greater_equal:
        return BinaryOperator::greater_equal;
    case TokenKind::plus:
        return BinaryOperator::add;
    case TokenKind::minus:
        return BinaryOperator::subtract;
    case TokenKind::star:
        return BinaryOperator::multiply;
    case TokenKind::slash:
        return BinaryOperator::divide;
    case TokenKind::percent:
        return BinaryOperator::remainder;
    default:
        return std::nullopt;
    }
}

std::optional<ScalarTypeKind> scalar_type(TokenKind kind) noexcept {
    switch (kind) {
    case TokenKind::keyword_int:
        return ScalarTypeKind::integer;
    case TokenKind::keyword_bool:
        return ScalarTypeKind::boolean;
    case TokenKind::keyword_char:
        return ScalarTypeKind::character;
    case TokenKind::keyword_string:
        return ScalarTypeKind::string;
    default:
        return std::nullopt;
    }
}

bool is_equality_operator(TokenKind kind) noexcept {
    return kind == TokenKind::equal || kind == TokenKind::not_equal;
}

bool is_comparison_operator(TokenKind kind) noexcept {
    return kind == TokenKind::less || kind == TokenKind::less_equal
        || kind == TokenKind::greater || kind == TokenKind::greater_equal;
}

bool can_start_expression(TokenKind kind) noexcept {
    switch (kind) {
    case TokenKind::identifier:
    case TokenKind::integer_literal:
    case TokenKind::boolean_literal:
    case TokenKind::string_literal:
    case TokenKind::character_literal:
    case TokenKind::left_parenthesis:
    case TokenKind::keyword_vector:
    case TokenKind::plus:
    case TokenKind::minus:
    case TokenKind::logical_not:
        return true;
    default:
        return false;
    }
}

bool is_expression_delimiter(TokenKind kind) noexcept {
    switch (kind) {
    case TokenKind::end_of_file:
    case TokenKind::right_parenthesis:
    case TokenKind::left_brace:
    case TokenKind::right_bracket:
    case TokenKind::right_brace:
    case TokenKind::comma:
    case TokenKind::semicolon:
    case TokenKind::assign:
    case TokenKind::plus_assign:
    case TokenKind::minus_assign:
    case TokenKind::star_assign:
    case TokenKind::slash_assign:
    case TokenKind::percent_assign:
    case TokenKind::range_exclusive:
    case TokenKind::range_inclusive:
    case TokenKind::keyword_int:
    case TokenKind::keyword_bool:
    case TokenKind::keyword_char:
    case TokenKind::keyword_string:
    case TokenKind::keyword_void:
    case TokenKind::keyword_if:
    case TokenKind::keyword_else:
    case TokenKind::keyword_while:
    case TokenKind::keyword_for:
    case TokenKind::keyword_in:
    case TokenKind::keyword_return:
    case TokenKind::keyword_break:
    case TokenKind::keyword_continue:
        return true;
    default:
        return false;
    }
}

}

ExpressionParser::ExpressionParser(
    std::span<const Token> tokens,
    const SourceManager& sources,
    DiagnosticEngine& diagnostics)
    : ExpressionParser{
          tokens,
          sources,
          diagnostics,
          ValidatedTokenStream{}} {
    if (tokens_.empty()
        || tokens_.back().kind != TokenKind::end_of_file) {
        throw std::invalid_argument{
            "expression parser requires a trailing EOF token"};
    }

    for (std::size_t index = 0; index + 1 < tokens_.size(); ++index) {
        if (tokens_[index].kind == TokenKind::end_of_file) {
            throw std::invalid_argument{
                "expression parser requires exactly one trailing EOF token"};
        }
    }
}

ExpressionParser::ExpressionParser(
    std::span<const Token> tokens,
    const SourceManager& sources,
    DiagnosticEngine& diagnostics,
    ValidatedTokenStream) noexcept
    : tokens_{tokens}
    , sources_{sources}
    , diagnostics_{diagnostics} {
}

ExpressionPtr ExpressionParser::parse() {
    auto expression = parse_prefix();

    if (!expression) {
        synchronize();
        return nullptr;
    }

    if (!at_end()) {
        report_at_current("unexpected token after expression");
        synchronize();
        return nullptr;
    }

    return expression;
}

ExpressionPtr ExpressionParser::parse_prefix() {
    return parse_expression();
}

std::size_t ExpressionParser::consumed_token_count() const noexcept {
    return index_;
}

ExpressionPtr ExpressionParser::parse_expression() {
    return parse_logical_or();
}

ExpressionPtr ExpressionParser::parse_logical_or() {
    auto expression = parse_logical_and();
    if (!expression) {
        return nullptr;
    }

    while (check(TokenKind::logical_or)) {
        const auto operator_kind = *binary_operator(advance().kind);
        auto right = parse_logical_and();
        if (!right) {
            return nullptr;
        }

        expression = make_binary(
            std::move(expression),
            operator_kind,
            std::move(right));
    }

    return expression;
}

ExpressionPtr ExpressionParser::parse_logical_and() {
    auto expression = parse_equality();
    if (!expression) {
        return nullptr;
    }

    while (check(TokenKind::logical_and)) {
        const auto operator_kind = *binary_operator(advance().kind);
        auto right = parse_equality();
        if (!right) {
            return nullptr;
        }

        expression = make_binary(
            std::move(expression),
            operator_kind,
            std::move(right));
    }

    return expression;
}

ExpressionPtr ExpressionParser::parse_equality() {
    auto expression = parse_comparison();
    if (!expression) {
        return nullptr;
    }

    if (is_equality_operator(current().kind)) {
        const auto operator_kind = *binary_operator(advance().kind);
        auto right = parse_comparison();
        if (!right) {
            return nullptr;
        }

        expression = make_binary(
            std::move(expression),
            operator_kind,
            std::move(right));
    }

    if (is_equality_operator(current().kind)) {
        report_at_current(
            "chained equality expressions are not allowed");
        return nullptr;
    }

    return expression;
}

ExpressionPtr ExpressionParser::parse_comparison() {
    auto expression = parse_additive();
    if (!expression) {
        return nullptr;
    }

    if (is_comparison_operator(current().kind)) {
        const auto operator_kind = *binary_operator(advance().kind);
        auto right = parse_additive();
        if (!right) {
            return nullptr;
        }

        expression = make_binary(
            std::move(expression),
            operator_kind,
            std::move(right));
    }

    if (is_comparison_operator(current().kind)) {
        report_at_current(
            "chained comparison expressions are not allowed");
        return nullptr;
    }

    return expression;
}

ExpressionPtr ExpressionParser::parse_additive() {
    auto expression = parse_multiplicative();
    if (!expression) {
        return nullptr;
    }

    while (check(TokenKind::plus) || check(TokenKind::minus)) {
        const auto operator_kind = *binary_operator(advance().kind);
        auto right = parse_multiplicative();
        if (!right) {
            return nullptr;
        }

        expression = make_binary(
            std::move(expression),
            operator_kind,
            std::move(right));
    }

    return expression;
}

ExpressionPtr ExpressionParser::parse_multiplicative() {
    auto expression = parse_unary();
    if (!expression) {
        return nullptr;
    }

    while (check(TokenKind::star) || check(TokenKind::slash)
           || check(TokenKind::percent)) {
        const auto operator_kind = *binary_operator(advance().kind);
        auto right = parse_unary();
        if (!right) {
            return nullptr;
        }

        expression = make_binary(
            std::move(expression),
            operator_kind,
            std::move(right));
    }

    return expression;
}

ExpressionPtr ExpressionParser::parse_unary() {
    const auto operator_kind = unary_operator(current().kind);
    if (!operator_kind.has_value()) {
        return parse_postfix();
    }

    const auto operator_span = advance().span;
    auto operand = parse_unary();
    if (!operand) {
        return nullptr;
    }

    const auto span = joined_span(operator_span, operand->span);
    return make_expression(
        span,
        UnaryExpression{
            .operator_kind = *operator_kind,
            .operand = std::move(operand),
        });
}

ExpressionPtr ExpressionParser::parse_postfix() {
    auto expression = parse_primary();
    if (!expression) {
        return nullptr;
    }

    while (true) {
        if (match(TokenKind::left_parenthesis)) {
            std::vector<ExpressionPtr> arguments;
            const auto closing_span = parse_argument_list(arguments);
            if (!closing_span.has_value()) {
                return nullptr;
            }

            const auto span = joined_span(
                expression->span,
                *closing_span);
            expression = make_expression(
                span,
                CallExpression{
                    .callee = std::move(expression),
                    .arguments = std::move(arguments),
                });
            continue;
        }

        if (match(TokenKind::left_bracket)) {
            auto index = parse_expression();
            if (!index) {
                return nullptr;
            }

            if (!match(TokenKind::right_bracket)) {
                report(
                    insertion_span(),
                    "expected ']' after index expression");
                return nullptr;
            }

            const auto span = joined_span(
                expression->span,
                previous().span);
            expression = make_expression(
                span,
                IndexExpression{
                    .base = std::move(expression),
                    .index = std::move(index),
                });
            continue;
        }

        if (match(TokenKind::dot)) {
            if (!check(TokenKind::identifier)) {
                report_at_current("expected member name after '.'");
                return nullptr;
            }

            const auto& member = advance();
            const auto span = joined_span(
                expression->span,
                member.span);
            expression = make_expression(
                span,
                MemberAccessExpression{
                    .base = std::move(expression),
                    .member = std::string{sources_.slice(member.span)},
                });
            continue;
        }

        return expression;
    }
}

ExpressionPtr ExpressionParser::parse_primary() {
    const auto& token = current();

    switch (token.kind) {
    case TokenKind::integer_literal:
        advance();
        return make_expression(
            token.span,
            IntegerLiteralExpression{
                .lexeme = std::string{sources_.slice(token.span)},
            });
    case TokenKind::boolean_literal:
        advance();
        return make_expression(
            token.span,
            BooleanLiteralExpression{
                .value = std::get<bool>(token.value),
            });
    case TokenKind::character_literal:
        advance();
        return make_expression(
            token.span,
            CharacterLiteralExpression{
                .value = std::get<char>(token.value),
            });
    case TokenKind::string_literal:
        advance();
        return make_expression(
            token.span,
            StringLiteralExpression{
                .value = std::get<std::string>(token.value),
            });
    case TokenKind::identifier:
        advance();
        return make_expression(
            token.span,
            IdentifierExpression{
                .name = std::string{sources_.slice(token.span)},
            });
    case TokenKind::left_parenthesis:
        return parse_parenthesized();
    case TokenKind::keyword_vector:
        return parse_vector_construction();
    default:
        report_at_current("expected expression");
        if (!is_expression_delimiter(token.kind)) {
            advance();
        }
        return nullptr;
    }
}

ExpressionPtr ExpressionParser::parse_parenthesized() {
    const auto opening_span = advance().span;
    auto expression = parse_expression();
    if (!expression) {
        return nullptr;
    }

    if (!match(TokenKind::right_parenthesis)) {
        report(
            insertion_span(),
            "expected ')' after parenthesized expression");
        return nullptr;
    }

    const auto span = joined_span(opening_span, previous().span);
    return make_expression(
        span,
        ParenthesizedExpression{
            .expression = std::move(expression),
        });
}

ExpressionPtr ExpressionParser::parse_vector_construction() {
    auto type = parse_value_type();
    if (!type.has_value()) {
        return nullptr;
    }

    if (!match(TokenKind::left_parenthesis)) {
        report(
            insertion_span(),
            "expected '(' after vector type");
        return nullptr;
    }

    std::vector<ExpressionPtr> arguments;
    const auto closing_span = parse_argument_list(arguments);
    if (!closing_span.has_value()) {
        return nullptr;
    }

    const auto span = joined_span(type->span, *closing_span);
    return make_expression(
        span,
        VectorConstructionExpression{
            .type = std::move(*type),
            .arguments = std::move(arguments),
        });
}

std::optional<ValueType> ExpressionParser::parse_value_type() {
    if (const auto scalar = scalar_type(current().kind);
        scalar.has_value()) {
        const auto span = advance().span;
        return ValueType{
            .span = span,
            .node = *scalar,
        };
    }

    if (!match(TokenKind::keyword_vector)) {
        return std::nullopt;
    }

    const auto opening_span = previous().span;

    if (!match(TokenKind::less)) {
        report(
            insertion_span(),
            "expected '<' after 'vector'");
        return std::nullopt;
    }

    auto element_type = parse_value_type();
    if (!element_type.has_value()) {
        report_at_current("expected value type in vector type");
        if (!at_end() && !check(TokenKind::greater)
            && (check(TokenKind::keyword_void)
                || !is_expression_delimiter(current().kind))) {
            advance();
        }
        return std::nullopt;
    }

    if (!match(TokenKind::greater)) {
        report(
            insertion_span(),
            "expected '>' after vector element type");
        return std::nullopt;
    }

    return ValueType{
        .span = joined_span(opening_span, previous().span),
        .node = VectorType{
            .element_type = std::make_unique<ValueType>(
                std::move(*element_type)),
        },
    };
}

std::optional<SourceSpan> ExpressionParser::parse_argument_list(
    std::vector<ExpressionPtr>& arguments) {
    if (match(TokenKind::right_parenthesis)) {
        return previous().span;
    }

    if (at_end()) {
        report(
            insertion_span(),
            "expected ')' after argument list");
        return std::nullopt;
    }

    while (true) {
        auto argument = parse_expression();
        if (!argument) {
            return std::nullopt;
        }
        arguments.push_back(std::move(argument));

        if (match(TokenKind::right_parenthesis)) {
            return previous().span;
        }

        if (at_end()) {
            report(
                insertion_span(),
                "expected ')' after argument list");
            return std::nullopt;
        }

        if (!match(TokenKind::comma)) {
            report_at_current("expected ',' or ')' after argument");
            return std::nullopt;
        }

        if (!can_start_expression(current().kind)) {
            report_at_current("expected expression after ','");
            return std::nullopt;
        }
    }
}

bool ExpressionParser::at_end() const noexcept {
    return current().kind == TokenKind::end_of_file;
}

bool ExpressionParser::check(TokenKind kind) const noexcept {
    return current().kind == kind;
}

bool ExpressionParser::match(TokenKind kind) noexcept {
    if (!check(kind)) {
        return false;
    }

    advance();
    return true;
}

const Token& ExpressionParser::current() const noexcept {
    return tokens_[index_];
}

const Token& ExpressionParser::previous() const noexcept {
    return tokens_[index_ - 1];
}

const Token& ExpressionParser::advance() noexcept {
    const auto& token = current();
    if (!at_end()) {
        ++index_;
    }
    return token;
}

SourceSpan ExpressionParser::insertion_span() const noexcept {
    return SourceSpan{
        .source = current().span.source,
        .begin = current().span.begin,
        .end = current().span.begin,
    };
}

void ExpressionParser::report(SourceSpan span, std::string message) {
    if (failed_) {
        return;
    }

    failed_ = true;
    diagnostics_.error(span, std::move(message));
}

void ExpressionParser::report_at_current(std::string message) {
    report(current().span, std::move(message));
}

void ExpressionParser::synchronize() noexcept {
    while (!at_end()) {
        advance();
    }
}

}

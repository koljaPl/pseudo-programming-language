#include "pseudo/parser/parser.hpp"

#include "pseudo/diagnostics/diagnostic_engine.hpp"
#include "pseudo/parser/expression_parser.hpp"
#include "pseudo/source/source_manager.hpp"

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace tpp {
namespace {

SourceSpan joined_span(SourceSpan first, SourceSpan last) noexcept {
    return SourceSpan{
        .source = first.source,
        .begin = first.begin,
        .end = last.end,
    };
}

bool is_scalar_type(TokenKind kind) noexcept {
    switch (kind) {
    case TokenKind::keyword_int:
    case TokenKind::keyword_bool:
    case TokenKind::keyword_char:
    case TokenKind::keyword_string:
        return true;
    default:
        return false;
    }
}

bool is_value_type_start(TokenKind kind) noexcept {
    return is_scalar_type(kind) || kind == TokenKind::keyword_vector;
}

bool is_return_type_start(TokenKind kind) noexcept {
    return is_value_type_start(kind) || kind == TokenKind::keyword_void;
}

bool is_assignment_operator(TokenKind kind) noexcept {
    switch (kind) {
    case TokenKind::assign:
    case TokenKind::plus_assign:
    case TokenKind::minus_assign:
    case TokenKind::star_assign:
    case TokenKind::slash_assign:
    case TokenKind::percent_assign:
        return true;
    default:
        return false;
    }
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

AssignmentOperator assignment_operator(TokenKind kind) {
    switch (kind) {
    case TokenKind::assign:
        return AssignmentOperator::assign;
    case TokenKind::plus_assign:
        return AssignmentOperator::add_assign;
    case TokenKind::minus_assign:
        return AssignmentOperator::subtract_assign;
    case TokenKind::star_assign:
        return AssignmentOperator::multiply_assign;
    case TokenKind::slash_assign:
        return AssignmentOperator::divide_assign;
    case TokenKind::percent_assign:
        return AssignmentOperator::remainder_assign;
    default:
        throw std::invalid_argument{"token is not an assignment operator"};
    }
}

bool is_strong_statement_start(TokenKind kind) noexcept {
    switch (kind) {
    case TokenKind::keyword_int:
    case TokenKind::keyword_bool:
    case TokenKind::keyword_char:
    case TokenKind::keyword_string:
    case TokenKind::keyword_void:
    case TokenKind::keyword_vector:
    case TokenKind::keyword_if:
    case TokenKind::keyword_while:
    case TokenKind::keyword_for:
    case TokenKind::keyword_return:
    case TokenKind::keyword_break:
    case TokenKind::keyword_continue:
    case TokenKind::left_brace:
        return true;
    default:
        return false;
    }
}

bool skip_value_type(
    std::span<const Token> tokens,
    std::size_t& index) noexcept {
    if (index >= tokens.size()) {
        return false;
    }

    if (is_scalar_type(tokens[index].kind)) {
        ++index;
        return true;
    }

    if (tokens[index].kind != TokenKind::keyword_vector) {
        return false;
    }

    ++index;
    if (index >= tokens.size()
        || tokens[index].kind != TokenKind::less) {
        return false;
    }
    ++index;

    if (!skip_value_type(tokens, index)) {
        return false;
    }

    if (index >= tokens.size()
        || tokens[index].kind != TokenKind::greater) {
        return false;
    }
    ++index;
    return true;
}

}

Parser::Parser(
    std::span<const Token> tokens,
    const SourceManager& sources,
    DiagnosticEngine& diagnostics)
    : tokens_{tokens}
    , sources_{sources}
    , diagnostics_{diagnostics} {
    if (tokens_.empty()
        || tokens_.back().kind != TokenKind::end_of_file) {
        throw std::invalid_argument{
            "parser requires a trailing EOF token"};
    }

    for (std::size_t index = 0; index + 1 < tokens_.size(); ++index) {
        if (tokens_[index].kind == TokenKind::end_of_file) {
            throw std::invalid_argument{
                "parser requires exactly one trailing EOF token"};
        }
    }
}

Program Parser::parse_program() {
    std::vector<TopLevelDeclaration> declarations;

    while (!at_end()) {
        const auto construct_begin = index_;
        auto declaration = parse_top_level_declaration();

        if (declaration.has_value()) {
            declarations.push_back(std::move(*declaration));
        } else {
            synchronize_top_level(construct_begin);
        }

        if (index_ == construct_begin && !at_end()) {
            advance();
        }
    }

    const auto eof = current().span;
    return Program{
        .span = SourceSpan{
            .source = eof.source,
            .begin = 0,
            .end = eof.end,
        },
        .declarations = std::move(declarations),
    };
}

std::optional<TopLevelDeclaration>
Parser::parse_top_level_declaration() {
    if (!is_return_type_start(current().kind)) {
        report_at_current("expected top-level declaration");
        return std::nullopt;
    }

    auto declaration = parse_typed_declaration();
    if (!declaration.has_value()) {
        return std::nullopt;
    }

    if (auto* function =
            std::get_if<FunctionDeclaration>(&*declaration)) {
        return TopLevelDeclaration{std::move(*function)};
    }

    return TopLevelDeclaration{
        std::move(std::get<VariableDeclaration>(*declaration))};
}

std::optional<Parser::ParsedDeclaration>
Parser::parse_typed_declaration() {
    auto return_type = parse_return_type();
    if (!return_type.has_value()) {
        return std::nullopt;
    }

    if (!check(TokenKind::identifier)) {
        report_at_current("expected declaration name after type");
        return std::nullopt;
    }

    const auto name = advance();
    if (match(TokenKind::left_parenthesis)) {
        auto function = parse_function(std::move(*return_type), name);
        if (!function.has_value()) {
            return std::nullopt;
        }
        return ParsedDeclaration{std::move(*function)};
    }

    if (std::holds_alternative<VoidType>(return_type->node)) {
        report(
            insertion_span(),
            "expected '(' after function name");
        return std::nullopt;
    }

    auto variable = parse_variable(
        std::move(std::get<ValueType>(return_type->node)),
        name);
    if (!variable.has_value()) {
        return std::nullopt;
    }
    return ParsedDeclaration{std::move(*variable)};
}

std::optional<FunctionDeclaration> Parser::parse_function(
    ReturnType return_type,
    const Token& name) {
    std::vector<Parameter> parameters;

    if (!match(TokenKind::right_parenthesis)) {
        if (at_end() || check(TokenKind::left_brace)) {
            report(
                insertion_span(),
                "expected ')' after parameter list");
            skip_malformed_function();
            return std::nullopt;
        }

        while (true) {
            auto parameter = parse_parameter();
            if (!parameter.has_value()) {
                skip_malformed_function();
                return std::nullopt;
            }
            parameters.push_back(std::move(*parameter));

            if (match(TokenKind::right_parenthesis)) {
                break;
            }

            if (at_end() || check(TokenKind::left_brace)) {
                report(
                    insertion_span(),
                    "expected ')' after parameter list");
                skip_malformed_function();
                return std::nullopt;
            }

            if (!match(TokenKind::comma)) {
                report_at_current(
                    "expected ',' or ')' after parameter");
                skip_malformed_function();
                return std::nullopt;
            }

            if (check(TokenKind::right_parenthesis)) {
                report_at_current("expected parameter after ','");
                skip_malformed_function();
                return std::nullopt;
            }
        }
    }

    if (!check(TokenKind::left_brace)) {
        report_at_current("expected function body");
        return std::nullopt;
    }

    auto body = parse_block();
    if (!body) {
        return std::nullopt;
    }

    return FunctionDeclaration{
        .span = joined_span(return_type.span, body->span),
        .return_type = std::move(return_type),
        .name = std::string{sources_.slice(name.span)},
        .name_span = name.span,
        .parameters = std::move(parameters),
        .body = std::move(body),
    };
}

std::optional<VariableDeclaration> Parser::parse_variable(
    ValueType type,
    const Token& name) {
    ExpressionPtr initializer;

    if (match(TokenKind::assign)) {
        initializer = parse_expression();
        if (!initializer) {
            return std::nullopt;
        }
    }

    const auto end_span = finish_statement(
        "expected ';' after variable declaration");

    return VariableDeclaration{
        .span = joined_span(type.span, end_span),
        .type = std::move(type),
        .name = std::string{sources_.slice(name.span)},
        .name_span = name.span,
        .initializer = std::move(initializer),
    };
}

std::optional<Parameter> Parser::parse_parameter() {
    if (!is_value_type_start(current().kind)) {
        report_at_current("expected parameter type");
        return std::nullopt;
    }

    auto type = parse_value_type();
    if (!type.has_value()) {
        return std::nullopt;
    }

    if (!check(TokenKind::identifier)) {
        report_at_current("expected parameter name after type");
        return std::nullopt;
    }

    const auto name = advance();
    return Parameter{
        .span = joined_span(type->span, name.span),
        .type = std::move(*type),
        .name = std::string{sources_.slice(name.span)},
        .name_span = name.span,
    };
}

std::optional<ValueType> Parser::parse_value_type() {
    ExpressionParser parser{
        tokens_.subspan(index_),
        sources_,
        diagnostics_,
        ExpressionParser::ValidatedTokenStream{}};
    auto type = parser.parse_value_type();
    index_ += parser.consumed_token_count();
    return type;
}

std::optional<ReturnType> Parser::parse_return_type() {
    if (match(TokenKind::keyword_void)) {
        return ReturnType{
            .span = previous().span,
            .node = VoidType{},
        };
    }

    if (!is_value_type_start(current().kind)) {
        report_at_current("expected return type");
        return std::nullopt;
    }

    auto type = parse_value_type();
    if (!type.has_value()) {
        return std::nullopt;
    }

    const auto span = type->span;
    return ReturnType{
        .span = span,
        .node = std::move(*type),
    };
}

bool Parser::looks_like_value_declaration() const noexcept {
    auto lookahead = index_;
    if (!skip_value_type(tokens_, lookahead)) {
        return false;
    }

    return lookahead < tokens_.size()
        && tokens_[lookahead].kind != TokenKind::left_parenthesis;
}

BlockPtr Parser::parse_block() {
    if (!match(TokenKind::left_brace)) {
        report_at_current("expected block");
        return nullptr;
    }

    const auto opening_span = previous().span;
    std::vector<BlockItem> items;

    while (!at_end() && !check(TokenKind::right_brace)
           && !check(TokenKind::keyword_else)) {
        const auto construct_begin = index_;
        auto item = parse_block_item();

        if (item.has_value()) {
            items.push_back(std::move(*item));
        } else {
            synchronize_statement(construct_begin);
        }

        if (index_ == construct_begin && !at_end()
            && !check(TokenKind::right_brace)
            && !check(TokenKind::keyword_else)) {
            advance();
        }
    }

    SourceSpan closing_span = insertion_span();
    if (match(TokenKind::right_brace)) {
        closing_span = previous().span;
    } else {
        report(
            insertion_span(),
            "expected '}' to close block");
    }

    return std::make_unique<Block>(Block{
        .span = joined_span(opening_span, closing_span),
        .items = std::move(items),
    });
}

std::optional<BlockItem> Parser::parse_block_item() {
    const auto typed_declaration =
        is_scalar_type(current().kind)
        || current().kind == TokenKind::keyword_void
        || (current().kind == TokenKind::keyword_vector
            && looks_like_value_declaration());

    if (typed_declaration) {
        auto declaration = parse_typed_declaration();
        if (!declaration.has_value()) {
            return std::nullopt;
        }

        if (auto* function =
                std::get_if<FunctionDeclaration>(&*declaration)) {
            return BlockItem{std::move(*function)};
        }

        auto variable =
            std::move(std::get<VariableDeclaration>(*declaration));
        const auto span = variable.span;
        return BlockItem{Statement{
            .span = span,
            .node = std::move(variable),
        }};
    }

    auto statement = parse_statement();
    if (!statement.has_value()) {
        return std::nullopt;
    }
    return BlockItem{std::move(*statement)};
}

std::optional<Statement> Parser::parse_statement() {
    switch (current().kind) {
    case TokenKind::keyword_if:
        return parse_if_statement();
    case TokenKind::keyword_while:
        return parse_while_statement();
    case TokenKind::keyword_for:
        return parse_for_statement();
    case TokenKind::keyword_return:
        return parse_return_statement();
    case TokenKind::keyword_break:
        return parse_break_statement();
    case TokenKind::keyword_continue:
        return parse_continue_statement();
    case TokenKind::left_brace:
        return parse_block_statement();
    default:
        return parse_expression_or_assignment_statement();
    }
}

std::optional<Statement> Parser::parse_if_statement() {
    const auto opening_span = advance().span;
    auto condition = parse_expression();
    if (!condition) {
        return std::nullopt;
    }

    if (!check(TokenKind::left_brace)) {
        report_at_current("expected block after if condition");
        return std::nullopt;
    }

    auto then_block = parse_block();
    if (!then_block) {
        return std::nullopt;
    }

    BlockPtr else_block;
    SourceSpan end_span = then_block->span;
    if (match(TokenKind::keyword_else)) {
        if (!check(TokenKind::left_brace)) {
            report_at_current("expected block after 'else'");
        } else {
            else_block = parse_block();
            if (else_block) {
                end_span = else_block->span;
            }
        }
    }

    return Statement{
        .span = joined_span(opening_span, end_span),
        .node = IfStatement{
            .condition = std::move(condition),
            .then_block = std::move(then_block),
            .else_block = std::move(else_block),
        },
    };
}

std::optional<Statement> Parser::parse_while_statement() {
    const auto opening_span = advance().span;
    auto condition = parse_expression();
    if (!condition) {
        return std::nullopt;
    }

    if (!check(TokenKind::left_brace)) {
        report_at_current("expected block after while condition");
        return std::nullopt;
    }

    auto body = parse_block();
    if (!body) {
        return std::nullopt;
    }

    const auto span = joined_span(opening_span, body->span);
    return Statement{
        .span = span,
        .node = WhileStatement{
            .condition = std::move(condition),
            .body = std::move(body),
        },
    };
}

std::optional<Statement> Parser::parse_for_statement() {
    const auto opening_span = advance().span;

    if (!check(TokenKind::identifier)) {
        report_at_current("expected loop variable after 'for'");
        return std::nullopt;
    }
    const auto variable = advance();

    if (!match(TokenKind::keyword_in)) {
        report_at_current("expected 'in' after loop variable");
        return std::nullopt;
    }

    auto first_expression = parse_expression();
    if (!first_expression) {
        return std::nullopt;
    }

    if (check(TokenKind::range_exclusive)
        || check(TokenKind::range_inclusive)) {
        const auto range_token = advance().kind;
        const auto range_kind =
            range_token == TokenKind::range_inclusive
                ? RangeOperator::inclusive
                : RangeOperator::exclusive;

        auto end = parse_expression();
        if (!end) {
            return std::nullopt;
        }

        if (!check(TokenKind::left_brace)) {
            report_at_current("expected block after for range");
            return std::nullopt;
        }

        auto body = parse_block();
        if (!body) {
            return std::nullopt;
        }

        const auto span = joined_span(opening_span, body->span);
        return Statement{
            .span = span,
            .node = ForRangeStatement{
                .variable = std::string{sources_.slice(variable.span)},
                .variable_span = variable.span,
                .begin = std::move(first_expression),
                .operator_kind = range_kind,
                .end = std::move(end),
                .body = std::move(body),
            },
        };
    }

    if (!check(TokenKind::left_brace)) {
        report_at_current(
            "expected range operator or block after for expression");
        return std::nullopt;
    }

    auto body = parse_block();
    if (!body) {
        return std::nullopt;
    }

    const auto span = joined_span(opening_span, body->span);
    return Statement{
        .span = span,
        .node = ForEachStatement{
            .variable = std::string{sources_.slice(variable.span)},
            .variable_span = variable.span,
            .iterable = std::move(first_expression),
            .body = std::move(body),
        },
    };
}

std::optional<Statement> Parser::parse_return_statement() {
    const auto opening_span = advance().span;
    ExpressionPtr value;

    if (!check(TokenKind::semicolon)) {
        if (!can_start_expression(current().kind)) {
            const auto end_span = finish_statement(
                "expected ';' after return statement");
            return Statement{
                .span = joined_span(opening_span, end_span),
                .node = ReturnStatement{.value = nullptr},
            };
        }

        value = parse_expression();
        if (!value) {
            return std::nullopt;
        }
    }

    const auto end_span = finish_statement(
        "expected ';' after return statement");
    return Statement{
        .span = joined_span(opening_span, end_span),
        .node = ReturnStatement{.value = std::move(value)},
    };
}

Statement Parser::parse_break_statement() {
    const auto opening_span = advance().span;
    const auto end_span = finish_statement(
        "expected ';' after break statement");
    return Statement{
        .span = joined_span(opening_span, end_span),
        .node = BreakStatement{},
    };
}

Statement Parser::parse_continue_statement() {
    const auto opening_span = advance().span;
    const auto end_span = finish_statement(
        "expected ';' after continue statement");
    return Statement{
        .span = joined_span(opening_span, end_span),
        .node = ContinueStatement{},
    };
}

std::optional<Statement> Parser::parse_block_statement() {
    auto block = parse_block();
    if (!block) {
        return std::nullopt;
    }

    const auto span = block->span;
    return Statement{
        .span = span,
        .node = BlockStatement{.block = std::move(block)},
    };
}

std::optional<Statement>
Parser::parse_expression_or_assignment_statement() {
    auto expression = parse_expression();
    if (!expression) {
        return std::nullopt;
    }

    const auto opening_span = expression->span;
    if (is_assignment_operator(current().kind)) {
        const auto operator_kind = assignment_operator(advance().kind);
        auto target = take_assignment_target(std::move(expression));
        if (!target.has_value()) {
            report(
                opening_span,
                "invalid assignment target; expected identifier or indexing chain");
            return std::nullopt;
        }

        auto value = parse_expression();
        if (!value) {
            return std::nullopt;
        }

        const auto end_span = finish_statement(
            "expected ';' after assignment");
        return Statement{
            .span = joined_span(opening_span, end_span),
            .node = AssignmentStatement{
                .target = std::move(*target),
                .operator_kind = operator_kind,
                .value = std::move(value),
            },
        };
    }

    const auto end_span = finish_statement(
        "expected ';' after expression");
    return Statement{
        .span = joined_span(opening_span, end_span),
        .node = ExpressionStatement{
            .expression = std::move(expression),
        },
    };
}

ExpressionPtr Parser::parse_expression() {
    ExpressionParser parser{
        tokens_.subspan(index_),
        sources_,
        diagnostics_,
        ExpressionParser::ValidatedTokenStream{}};
    auto expression = parser.parse_prefix();
    index_ += parser.consumed_token_count();
    return expression;
}

std::optional<AssignmentTarget> Parser::take_assignment_target(
    ExpressionPtr expression) {
    const auto target_span = expression->span;
    std::vector<ExpressionPtr> indices;

    while (auto* index =
               std::get_if<IndexExpression>(&expression->node)) {
        auto base = std::move(index->base);
        indices.push_back(std::move(index->index));
        expression = std::move(base);
    }

    auto* identifier =
        std::get_if<IdentifierExpression>(&expression->node);
    if (identifier == nullptr) {
        return std::nullopt;
    }

    std::reverse(indices.begin(), indices.end());
    return AssignmentTarget{
        .span = target_span,
        .name = std::move(identifier->name),
        .name_span = expression->span,
        .indices = std::move(indices),
    };
}

bool Parser::at_end() const noexcept {
    return current().kind == TokenKind::end_of_file;
}

bool Parser::check(TokenKind kind) const noexcept {
    return current().kind == kind;
}

bool Parser::match(TokenKind kind) noexcept {
    if (!check(kind)) {
        return false;
    }
    advance();
    return true;
}

const Token& Parser::current() const noexcept {
    return tokens_[index_];
}

const Token& Parser::previous() const noexcept {
    return tokens_[index_ - 1];
}

const Token& Parser::advance() noexcept {
    const auto& token = current();
    if (!at_end()) {
        ++index_;
    }
    return token;
}

SourceSpan Parser::insertion_span() const noexcept {
    return SourceSpan{
        .source = current().span.source,
        .begin = current().span.begin,
        .end = current().span.begin,
    };
}

SourceSpan Parser::finish_statement(std::string_view message) {
    if (match(TokenKind::semicolon)) {
        return previous().span;
    }

    report(insertion_span(), std::string{message});
    return previous().span;
}

void Parser::report(SourceSpan span, std::string message) {
    diagnostics_.error(span, std::move(message));
}

void Parser::report_at_current(std::string message) {
    report(current().span, std::move(message));
}

void Parser::synchronize_top_level(
    const std::size_t construct_begin) noexcept {
    if (index_ > construct_begin
        && (previous().kind == TokenKind::semicolon
            || previous().kind == TokenKind::right_brace)) {
        return;
    }

    while (!at_end()) {
        if (match(TokenKind::semicolon)) {
            return;
        }

        if (index_ > construct_begin
            && is_return_type_start(current().kind)) {
            return;
        }

        advance();
    }
}

void Parser::synchronize_statement(
    const std::size_t construct_begin) noexcept {
    if (index_ > construct_begin
        && (previous().kind == TokenKind::semicolon
            || previous().kind == TokenKind::right_brace)) {
        return;
    }

    while (!at_end() && !check(TokenKind::right_brace)
           && !check(TokenKind::keyword_else)) {
        if (match(TokenKind::semicolon)) {
            return;
        }

        if (index_ > construct_begin
            && is_strong_statement_start(current().kind)) {
            return;
        }

        advance();
    }
}

void Parser::skip_malformed_function() noexcept {
    while (!at_end() && !check(TokenKind::right_brace)) {
        if (match(TokenKind::semicolon)) {
            return;
        }

        if (!match(TokenKind::left_brace)) {
            advance();
            continue;
        }

        std::size_t depth = 1;
        while (!at_end() && depth != 0) {
            if (match(TokenKind::left_brace)) {
                ++depth;
            } else if (match(TokenKind::right_brace)) {
                --depth;
            } else {
                advance();
            }
        }
        return;
    }
}

}

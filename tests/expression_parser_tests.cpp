#include "test_support.hpp"

#include "pseudo/ast/expression.hpp"
#include "pseudo/diagnostics/diagnostic_engine.hpp"
#include "pseudo/lexer/lexer.hpp"
#include "pseudo/lexer/token.hpp"
#include "pseudo/parser/expression_parser.hpp"
#include "pseudo/source/source_manager.hpp"

#include <array>
#include <cstddef>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

using tpp::BinaryExpression;
using tpp::BinaryOperator;
using tpp::BooleanLiteralExpression;
using tpp::CallExpression;
using tpp::CharacterLiteralExpression;
using tpp::DiagnosticEngine;
using tpp::Expression;
using tpp::ExpressionParser;
using tpp::ExpressionPtr;
using tpp::IdentifierExpression;
using tpp::IndexExpression;
using tpp::IntegerLiteralExpression;
using tpp::Lexer;
using tpp::MemberAccessExpression;
using tpp::ParenthesizedExpression;
using tpp::ScalarTypeKind;
using tpp::SourceId;
using tpp::SourceManager;
using tpp::SourceSpan;
using tpp::StringLiteralExpression;
using tpp::Token;
using tpp::TokenKind;
using tpp::UnaryExpression;
using tpp::UnaryOperator;
using tpp::ValueType;
using tpp::VectorConstructionExpression;
using tpp::VectorType;

class ParsingResult {
public:
    explicit ParsingResult(
        std::string contents,
        std::string display_name = "expression.tpp",
        const bool use_second_source = false)
    {
        if (use_second_source) {
            static_cast<void>(
                sources.add_source("unused.tpp", "unused"));
        }

        source = sources.add_source(
            std::move(display_name),
            std::move(contents));

        Lexer lexer{source, sources, diagnostics};
        tokens = lexer.lex();
        TPP_CHECK(!diagnostics.has_errors());

        ExpressionParser parser{tokens, sources, diagnostics};
        expression = parser.parse();
    }

    SourceManager sources;
    DiagnosticEngine diagnostics;
    SourceId source{0};
    std::vector<Token> tokens;
    ExpressionPtr expression;
};

template <typename Node>
const Node& require_node(const Expression& expression)
{
    const auto* node = std::get_if<Node>(&expression.node);
    TPP_CHECK(node != nullptr);
    return *node;
}

template <typename Node>
const Node& require_type_node(const ValueType& type)
{
    const auto* node = std::get_if<Node>(&type.node);
    TPP_CHECK(node != nullptr);
    return *node;
}

const Expression& require_expression(const ExpressionPtr& expression)
{
    TPP_CHECK(expression != nullptr);
    return *expression;
}

const Expression& require_success(const ParsingResult& result)
{
    TPP_CHECK(!result.diagnostics.has_errors());
    return require_expression(result.expression);
}

const IdentifierExpression& require_identifier(
    const Expression& expression,
    const std::string_view expected_name)
{
    const auto& identifier =
        require_node<IdentifierExpression>(expression);
    TPP_CHECK_EQ(identifier.name, expected_name);
    return identifier;
}

const BinaryExpression& require_binary(
    const Expression& expression,
    const BinaryOperator expected_operator)
{
    const auto& binary = require_node<BinaryExpression>(expression);
    TPP_CHECK_EQ(binary.operator_kind, expected_operator);
    TPP_CHECK(binary.left != nullptr);
    TPP_CHECK(binary.right != nullptr);
    return binary;
}

const UnaryExpression& require_unary(
    const Expression& expression,
    const UnaryOperator expected_operator)
{
    const auto& unary = require_node<UnaryExpression>(expression);
    TPP_CHECK_EQ(unary.operator_kind, expected_operator);
    TPP_CHECK(unary.operand != nullptr);
    return unary;
}

void check_span(
    const SourceSpan span,
    const SourceId source,
    const std::size_t begin,
    const std::size_t end)
{
    TPP_CHECK(span.source == source);
    TPP_CHECK_EQ(span.begin, begin);
    TPP_CHECK_EQ(span.end, end);
}

void check_single_error(
    const ParsingResult& result,
    const std::string_view expected_message,
    const std::size_t begin,
    const std::size_t end)
{
    TPP_CHECK(result.expression == nullptr);
    TPP_CHECK(result.diagnostics.has_errors());
    TPP_CHECK_EQ(result.diagnostics.error_count(), std::size_t{1});

    const auto diagnostics = result.diagnostics.diagnostics();
    TPP_CHECK_EQ(diagnostics.size(), std::size_t{1});
    TPP_CHECK_EQ(diagnostics.front().message, expected_message);
    TPP_CHECK(diagnostics.front().primary_span.has_value());
    check_span(
        *diagnostics.front().primary_span,
        result.source,
        begin,
        end);
}

ExpressionPtr parse_detached(std::string contents)
{
    ParsingResult result{std::move(contents)};
    TPP_CHECK(!result.diagnostics.has_errors());
    return std::move(result.expression);
}

void manually_constructed_tokens_are_parsed_without_the_lexer()
{
    SourceManager sources;
    DiagnosticEngine diagnostics;
    const auto source = sources.add_source("unit.tpp", "42");
    const std::vector<Token> tokens{
        Token{
            .kind = TokenKind::integer_literal,
            .span = SourceSpan{
                .source = source,
                .begin = 0,
                .end = 2,
            },
            .value = std::monostate{},
        },
        Token{
            .kind = TokenKind::end_of_file,
            .span = SourceSpan{
                .source = source,
                .begin = 2,
                .end = 2,
            },
            .value = std::monostate{},
        },
    };

    ExpressionParser parser{tokens, sources, diagnostics};
    const auto expression = parser.parse();

    TPP_CHECK(!diagnostics.has_errors());
    const auto& integer =
        require_node<IntegerLiteralExpression>(
            require_expression(expression));
    TPP_CHECK_EQ(integer.lexeme, std::string{"42"});
}

void literal_and_identifier_nodes_preserve_owned_values()
{
    const ParsingResult integer{"0007"};
    TPP_CHECK_EQ(
        require_node<IntegerLiteralExpression>(
            require_success(integer))
            .lexeme,
        std::string{"0007"});
    check_span(integer.expression->span, integer.source, 0, 4);

    const ParsingResult true_literal{"true"};
    const ParsingResult false_literal{"false"};
    TPP_CHECK_EQ(
        require_node<BooleanLiteralExpression>(
            require_success(true_literal))
            .value,
        true);
    TPP_CHECK_EQ(
        require_node<BooleanLiteralExpression>(
            require_success(false_literal))
            .value,
        false);
    check_span(
        true_literal.expression->span,
        true_literal.source,
        0,
        4);
    check_span(
        false_literal.expression->span,
        false_literal.source,
        0,
        5);

    const ParsingResult character{R"pseudo('\0')pseudo"};
    TPP_CHECK_EQ(
        require_node<CharacterLiteralExpression>(
            require_success(character))
            .value,
        '\0');
    check_span(character.expression->span, character.source, 0, 4);

    const ParsingResult string{R"pseudo("a\0b")pseudo"};
    const auto& string_value =
        require_node<StringLiteralExpression>(
            require_success(string))
            .value;
    TPP_CHECK_EQ(string_value, std::string("a\0b", 3));
    TPP_CHECK_EQ(string_value.size(), std::size_t{3});
    check_span(string.expression->span, string.source, 0, 6);

    const ParsingResult identifier{"alpha_1"};
    require_identifier(require_success(identifier), "alpha_1");
    check_span(identifier.expression->span, identifier.source, 0, 7);
}

void integer_literals_are_not_converted_to_signed_values()
{
    const ParsingResult positive{"9223372036854775808"};
    TPP_CHECK_EQ(
        require_node<IntegerLiteralExpression>(
            require_success(positive))
            .lexeme,
        std::string{"9223372036854775808"});

    const ParsingResult negative{"-9223372036854775808"};
    const auto& unary =
        require_unary(require_success(negative), UnaryOperator::minus);
    const auto& integer =
        require_node<IntegerLiteralExpression>(*unary.operand);
    TPP_CHECK_EQ(
        integer.lexeme,
        std::string{"9223372036854775808"});
    check_span(negative.expression->span, negative.source, 0, 20);
    check_span(unary.operand->span, negative.source, 1, 20);
}

void ast_values_outlive_lexer_and_source_manager()
{
    const auto identifier = parse_detached("persistent_name");
    TPP_CHECK_EQ(
        require_node<IdentifierExpression>(*identifier).name,
        std::string{"persistent_name"});

    const auto integer = parse_detached("00042");
    TPP_CHECK_EQ(
        require_node<IntegerLiteralExpression>(*integer).lexeme,
        std::string{"00042"});

    const auto string = parse_detached(R"pseudo("a\0b")pseudo");
    TPP_CHECK_EQ(
        require_node<StringLiteralExpression>(*string).value,
        std::string("a\0b", 3));
}

void unary_operator_spellings_map_to_typed_operators()
{
    struct Case {
        std::string_view source;
        UnaryOperator operator_kind;
    };

    constexpr std::array cases{
        Case{"+x", UnaryOperator::plus},
        Case{"-x", UnaryOperator::minus},
        Case{"!x", UnaryOperator::logical_not},
        Case{"not x", UnaryOperator::logical_not},
    };

    for (const auto& test_case : cases) {
        const ParsingResult result{std::string{test_case.source}};
        const auto& unary =
            require_unary(
                require_success(result),
                test_case.operator_kind);
        require_identifier(*unary.operand, "x");
    }
}

void binary_operator_spellings_map_to_typed_operators()
{
    struct Case {
        std::string_view source;
        BinaryOperator operator_kind;
    };

    constexpr std::array cases{
        Case{"a or b", BinaryOperator::logical_or},
        Case{"a || b", BinaryOperator::logical_or},
        Case{"a and b", BinaryOperator::logical_and},
        Case{"a && b", BinaryOperator::logical_and},
        Case{"a == b", BinaryOperator::equal},
        Case{"a != b", BinaryOperator::not_equal},
        Case{"a < b", BinaryOperator::less},
        Case{"a <= b", BinaryOperator::less_equal},
        Case{"a > b", BinaryOperator::greater},
        Case{"a >= b", BinaryOperator::greater_equal},
        Case{"a + b", BinaryOperator::add},
        Case{"a - b", BinaryOperator::subtract},
        Case{"a * b", BinaryOperator::multiply},
        Case{"a / b", BinaryOperator::divide},
        Case{"a % b", BinaryOperator::remainder},
    };

    for (const auto& test_case : cases) {
        const ParsingResult result{std::string{test_case.source}};
        const auto& binary =
            require_binary(
                require_success(result),
                test_case.operator_kind);
        require_identifier(*binary.left, "a");
        require_identifier(*binary.right, "b");
    }
}

void precedence_matches_the_ebnf_levels()
{
    const ParsingResult result{
        "a or b and c == d < e + f * -g"};

    const auto& logical_or = require_binary(
        require_success(result),
        BinaryOperator::logical_or);
    require_identifier(*logical_or.left, "a");

    const auto& logical_and = require_binary(
        *logical_or.right,
        BinaryOperator::logical_and);
    require_identifier(*logical_and.left, "b");

    const auto& equality =
        require_binary(*logical_and.right, BinaryOperator::equal);
    require_identifier(*equality.left, "c");

    const auto& comparison =
        require_binary(*equality.right, BinaryOperator::less);
    require_identifier(*comparison.left, "d");

    const auto& addition =
        require_binary(*comparison.right, BinaryOperator::add);
    require_identifier(*addition.left, "e");

    const auto& multiplication =
        require_binary(*addition.right, BinaryOperator::multiply);
    require_identifier(*multiplication.left, "f");

    const auto& unary =
        require_unary(*multiplication.right, UnaryOperator::minus);
    require_identifier(*unary.operand, "g");
}

void repeated_binary_levels_are_left_associative()
{
    struct Case {
        std::string_view source;
        BinaryOperator outer;
        BinaryOperator inner;
    };

    constexpr std::array cases{
        Case{
            "a or b or c",
            BinaryOperator::logical_or,
            BinaryOperator::logical_or,
        },
        Case{
            "a and b and c",
            BinaryOperator::logical_and,
            BinaryOperator::logical_and,
        },
        Case{
            "a - b - c",
            BinaryOperator::subtract,
            BinaryOperator::subtract,
        },
        Case{
            "a / b % c",
            BinaryOperator::remainder,
            BinaryOperator::divide,
        },
    };

    for (const auto& test_case : cases) {
        const ParsingResult result{std::string{test_case.source}};
        const auto& outer =
            require_binary(require_success(result), test_case.outer);
        const auto& inner =
            require_binary(*outer.left, test_case.inner);
        require_identifier(*inner.left, "a");
        require_identifier(*inner.right, "b");
        require_identifier(*outer.right, "c");
    }
}

void unary_expressions_are_right_associative()
{
    const ParsingResult result{"-+!not x"};
    const auto& minus =
        require_unary(require_success(result), UnaryOperator::minus);
    const auto& plus =
        require_unary(*minus.operand, UnaryOperator::plus);
    const auto& symbolic_not =
        require_unary(*plus.operand, UnaryOperator::logical_not);
    const auto& textual_not =
        require_unary(
            *symbolic_not.operand,
            UnaryOperator::logical_not);
    require_identifier(*textual_not.operand, "x");
}

void parentheses_are_explicit_and_override_precedence()
{
    const ParsingResult result{"(a + b) * c"};
    const auto& multiplication = require_binary(
        require_success(result),
        BinaryOperator::multiply);
    const auto& parentheses =
        require_node<ParenthesizedExpression>(*multiplication.left);
    const auto& addition =
        require_binary(
            *parentheses.expression,
            BinaryOperator::add);

    require_identifier(*addition.left, "a");
    require_identifier(*addition.right, "b");
    require_identifier(*multiplication.right, "c");
    check_span(multiplication.left->span, result.source, 0, 7);
    check_span(parentheses.expression->span, result.source, 1, 6);
}

void calls_support_arguments_and_repeated_suffixes()
{
    const ParsingResult empty{"f()"};
    const auto& empty_call =
        require_node<CallExpression>(require_success(empty));
    require_identifier(*empty_call.callee, "f");
    TPP_CHECK(empty_call.arguments.empty());

    const ParsingResult multiple{"f(a, b + c, g())"};
    const auto& call =
        require_node<CallExpression>(require_success(multiple));
    TPP_CHECK_EQ(call.arguments.size(), std::size_t{3});
    require_identifier(*call.arguments[0], "a");
    require_binary(*call.arguments[1], BinaryOperator::add);
    const auto& nested =
        require_node<CallExpression>(*call.arguments[2]);
    require_identifier(*nested.callee, "g");
    TPP_CHECK(nested.arguments.empty());

    const ParsingResult repeated{"(f)(x)()"};
    const auto& outer =
        require_node<CallExpression>(require_success(repeated));
    TPP_CHECK(outer.arguments.empty());
    const auto& inner =
        require_node<CallExpression>(*outer.callee);
    TPP_CHECK_EQ(inner.arguments.size(), std::size_t{1});
    require_identifier(*inner.arguments.front(), "x");
    const auto& parenthesized =
        require_node<ParenthesizedExpression>(*inner.callee);
    require_identifier(*parenthesized.expression, "f");
}

void chained_postfix_nodes_have_full_spans()
{
    const ParsingResult result{"foo(a)[i].length()"};
    const auto& outer_call =
        require_node<CallExpression>(require_success(result));
    TPP_CHECK(outer_call.arguments.empty());
    check_span(result.expression->span, result.source, 0, 18);

    const auto& member =
        require_node<MemberAccessExpression>(*outer_call.callee);
    TPP_CHECK_EQ(member.member, std::string{"length"});
    check_span(outer_call.callee->span, result.source, 0, 16);

    const auto& index =
        require_node<IndexExpression>(*member.base);
    require_identifier(*index.index, "i");
    check_span(member.base->span, result.source, 0, 9);
    check_span(index.index->span, result.source, 7, 8);

    const auto& inner_call =
        require_node<CallExpression>(*index.base);
    require_identifier(*inner_call.callee, "foo");
    TPP_CHECK_EQ(inner_call.arguments.size(), std::size_t{1});
    require_identifier(*inner_call.arguments.front(), "a");
    check_span(index.base->span, result.source, 0, 6);
    check_span(inner_call.callee->span, result.source, 0, 3);
    check_span(
        inner_call.arguments.front()->span,
        result.source,
        4,
        5);
}

void indexing_suffixes_chain_left_to_right()
{
    const ParsingResult result{"matrix[i][j]"};
    const auto& outer =
        require_node<IndexExpression>(require_success(result));
    require_identifier(*outer.index, "j");
    check_span(result.expression->span, result.source, 0, 12);

    const auto& inner =
        require_node<IndexExpression>(*outer.base);
    require_identifier(*inner.base, "matrix");
    require_identifier(*inner.index, "i");
    check_span(outer.base->span, result.source, 0, 9);
}

void vector_constructions_cover_types_arguments_and_postfix()
{
    struct ScalarCase {
        std::string_view spelling;
        ScalarTypeKind kind;
    };

    constexpr std::array scalar_cases{
        ScalarCase{"int", ScalarTypeKind::integer},
        ScalarCase{"bool", ScalarTypeKind::boolean},
        ScalarCase{"char", ScalarTypeKind::character},
        ScalarCase{"string", ScalarTypeKind::string},
    };

    for (const auto& test_case : scalar_cases) {
        const ParsingResult result{
            "vector<" + std::string{test_case.spelling} + ">()"};
        const auto& construction =
            require_node<VectorConstructionExpression>(
                require_success(result));
        const auto& vector =
            require_type_node<VectorType>(construction.type);
        const auto* scalar =
            std::get_if<ScalarTypeKind>(
                &vector.element_type->node);
        TPP_CHECK(scalar != nullptr);
        TPP_CHECK_EQ(*scalar, test_case.kind);
        TPP_CHECK(construction.arguments.empty());
    }

    const ParsingResult nested{"vector<vector<string>>(n)"};
    const auto& nested_construction =
        require_node<VectorConstructionExpression>(
            require_success(nested));
    const auto& outer_type =
        require_type_node<VectorType>(nested_construction.type);
    const auto* nested_type = std::get_if<VectorType>(
        &outer_type.element_type->node);
    TPP_CHECK(nested_type != nullptr);
    const auto* scalar =
        std::get_if<ScalarTypeKind>(
            &nested_type->element_type->node);
    TPP_CHECK(scalar != nullptr);
    TPP_CHECK_EQ(*scalar, ScalarTypeKind::string);
    check_span(nested_construction.type.span, nested.source, 0, 22);
    check_span(outer_type.element_type->span, nested.source, 7, 21);

    const ParsingResult initialized{"vector<int>(n, 0)"};
    const auto& initialized_node =
        require_node<VectorConstructionExpression>(
            require_success(initialized));
    TPP_CHECK_EQ(initialized_node.arguments.size(), std::size_t{2});
    require_identifier(*initialized_node.arguments[0], "n");
    TPP_CHECK_EQ(
        require_node<IntegerLiteralExpression>(
            *initialized_node.arguments[1])
            .lexeme,
        std::string{"0"});
    check_span(
        initialized.expression->span,
        initialized.source,
        0,
        initialized.sources.contents(initialized.source).size());

    const ParsingResult postfix{"vector<int>(a,b,c)[i]"};
    const auto& index =
        require_node<IndexExpression>(require_success(postfix));
    require_identifier(*index.index, "i");
    const auto& construction =
        require_node<VectorConstructionExpression>(*index.base);
    TPP_CHECK_EQ(construction.arguments.size(), std::size_t{3});

    const ParsingResult comparison{"vector<int>(n) > 0"};
    const auto& greater =
        require_binary(
            require_success(comparison),
            BinaryOperator::greater);
    require_node<VectorConstructionExpression>(*greater.left);
    TPP_CHECK_EQ(
        require_node<IntegerLiteralExpression>(*greater.right).lexeme,
        std::string{"0"});
}

void nested_spans_preserve_the_selected_source_id()
{
    const ParsingResult result{
        "a + b * c",
        "second.tpp",
        true};
    TPP_CHECK_EQ(result.source.value, std::size_t{1});

    const auto& addition =
        require_binary(require_success(result), BinaryOperator::add);
    check_span(result.expression->span, result.source, 0, 9);
    check_span(addition.left->span, result.source, 0, 1);

    const auto& multiplication =
        require_binary(*addition.right, BinaryOperator::multiply);
    check_span(addition.right->span, result.source, 4, 9);
    check_span(multiplication.left->span, result.source, 4, 5);
    check_span(multiplication.right->span, result.source, 8, 9);
}

void equality_and_comparison_follow_ebnf_cardinality()
{
    const ParsingResult valid{"a < b == c > d"};
    const auto& equality =
        require_binary(require_success(valid), BinaryOperator::equal);
    require_binary(*equality.left, BinaryOperator::less);
    require_binary(*equality.right, BinaryOperator::greater);

    const ParsingResult parenthesized{"(a < b) < c"};
    const auto& outer =
        require_binary(
            require_success(parenthesized),
            BinaryOperator::less);
    const auto& grouping =
        require_node<ParenthesizedExpression>(*outer.left);
    require_binary(*grouping.expression, BinaryOperator::less);

    struct InvalidCase {
        std::string_view source;
        std::string_view message;
        std::size_t begin;
        std::size_t end;
    };

    constexpr std::array invalid_cases{
        InvalidCase{
            "a < b < c",
            "chained comparison expressions are not allowed",
            6,
            7,
        },
        InvalidCase{
            "a <= b > c",
            "chained comparison expressions are not allowed",
            7,
            8,
        },
        InvalidCase{
            "a == b != c",
            "chained equality expressions are not allowed",
            7,
            9,
        },
        InvalidCase{
            "a != b == c",
            "chained equality expressions are not allowed",
            7,
            9,
        },
    };

    for (const auto& test_case : invalid_cases) {
        const ParsingResult result{std::string{test_case.source}};
        check_single_error(
            result,
            test_case.message,
            test_case.begin,
            test_case.end);
    }
}

void assignment_range_and_trailing_tokens_are_rejected()
{
    struct Case {
        std::string_view source;
        std::size_t begin;
        std::size_t end;
    };

    constexpr std::array cases{
        Case{"a = b", 2, 3},
        Case{"a += b", 2, 4},
        Case{"a .. b", 2, 4},
        Case{"a ..= b", 2, 5},
        Case{"a;", 1, 2},
        Case{"a)", 1, 2},
    };

    for (const auto& test_case : cases) {
        const ParsingResult result{std::string{test_case.source}};
        check_single_error(
            result,
            "unexpected token after expression",
            test_case.begin,
            test_case.end);
    }
}

void missing_and_unexpected_primaries_have_precise_errors()
{
    struct Case {
        std::string_view source;
        std::size_t begin;
        std::size_t end;
    };

    constexpr std::array cases{
        Case{"", 0, 0},
        Case{")", 0, 1},
        Case{"a +", 3, 3},
        Case{"a + * b", 4, 5},
        Case{"!", 1, 1},
        Case{"()", 1, 2},
    };

    for (const auto& test_case : cases) {
        const ParsingResult result{std::string{test_case.source}};
        check_single_error(
            result,
            "expected expression",
            test_case.begin,
            test_case.end);
    }
}

void missing_closing_delimiters_use_insertion_spans()
{
    struct Case {
        std::string_view source;
        std::string_view message;
        std::size_t offset;
    };

    constexpr std::array cases{
        Case{
            "(a",
            "expected ')' after parenthesized expression",
            2,
        },
        Case{
            "f(",
            "expected ')' after argument list",
            2,
        },
        Case{
            "f(a",
            "expected ')' after argument list",
            3,
        },
        Case{
            "a[i",
            "expected ']' after index expression",
            3,
        },
        Case{
            "a[i)",
            "expected ']' after index expression",
            3,
        },
    };

    for (const auto& test_case : cases) {
        const ParsingResult result{std::string{test_case.source}};
        check_single_error(
            result,
            test_case.message,
            test_case.offset,
            test_case.offset);
    }
}

void malformed_argument_lists_have_contextual_diagnostics()
{
    struct Case {
        std::string_view source;
        std::string_view message;
        std::size_t begin;
        std::size_t end;
    };

    constexpr std::array cases{
        Case{"f(a,)", "expected expression after ','", 4, 5},
        Case{
            "f(a b)",
            "expected ',' or ')' after argument",
            4,
            5,
        },
        Case{"f(,a)", "expected expression", 2, 3},
        Case{"f(a,,b)", "expected expression after ','", 4, 5},
        Case{"f(a +, b)", "expected expression", 5, 6},
    };

    for (const auto& test_case : cases) {
        const ParsingResult result{std::string{test_case.source}};
        check_single_error(
            result,
            test_case.message,
            test_case.begin,
            test_case.end);
    }
}

void malformed_postfix_suffixes_have_contextual_diagnostics()
{
    struct Case {
        std::string_view source;
        std::string_view message;
        std::size_t begin;
        std::size_t end;
    };

    constexpr std::array cases{
        Case{"a.", "expected member name after '.'", 2, 2},
        Case{"a.42", "expected member name after '.'", 2, 4},
        Case{"a.int", "expected member name after '.'", 2, 5},
        Case{"a[", "expected expression", 2, 2},
        Case{"a[]", "expected expression", 2, 3},
    };

    for (const auto& test_case : cases) {
        const ParsingResult result{std::string{test_case.source}};
        check_single_error(
            result,
            test_case.message,
            test_case.begin,
            test_case.end);
    }
}

void malformed_vector_types_have_contextual_diagnostics()
{
    struct Case {
        std::string_view source;
        std::string_view message;
        std::size_t begin;
        std::size_t end;
    };

    constexpr std::array cases{
        Case{"vector", "expected '<' after 'vector'", 6, 6},
        Case{
            "vector<",
            "expected value type in vector type",
            7,
            7,
        },
        Case{
            "vector<void>()",
            "expected value type in vector type",
            7,
            11,
        },
        Case{
            "vector<name>()",
            "expected value type in vector type",
            7,
            11,
        },
        Case{
            "vector<int",
            "expected '>' after vector element type",
            10,
            10,
        },
        Case{
            "vector<int>",
            "expected '(' after vector type",
            11,
            11,
        },
        Case{
            "vector<int>(n,)",
            "expected expression after ','",
            14,
            15,
        },
    };

    for (const auto& test_case : cases) {
        const ParsingResult result{std::string{test_case.source}};
        check_single_error(
            result,
            test_case.message,
            test_case.begin,
            test_case.end);
    }
}

void recovery_stops_after_one_error_and_does_not_pass_eof()
{
    constexpr std::array sources{
        std::string_view{"f(a,, b)"},
        std::string_view{"f(a +, b)"},
        std::string_view{"f(a., b)"},
        std::string_view{"f(a[], b)"},
        std::string_view{"f((a, b)"},
        std::string_view{"f(a b, c)"},
    };

    for (const auto source : sources) {
        const ParsingResult result{std::string{source}};
        TPP_CHECK(result.expression == nullptr);
        TPP_CHECK_EQ(
            result.diagnostics.error_count(),
            std::size_t{1});
        TPP_CHECK_EQ(
            result.diagnostics.diagnostics().size(),
            std::size_t{1});
    }

    SourceManager source_manager;
    DiagnosticEngine diagnostics;
    const auto source =
        source_manager.add_source("recovery.tpp", "f(a,,b)");
    Lexer lexer{source, source_manager, diagnostics};
    const auto tokens = lexer.lex();
    TPP_CHECK(!diagnostics.has_errors());

    ExpressionParser parser{tokens, source_manager, diagnostics};
    TPP_CHECK(parser.parse() == nullptr);
    TPP_CHECK_EQ(diagnostics.error_count(), std::size_t{1});
    TPP_CHECK(parser.parse() == nullptr);
    TPP_CHECK_EQ(diagnostics.error_count(), std::size_t{1});
}

void rendered_syntax_diagnostic_format_is_stable()
{
    const ParsingResult result{"f(a,)"};
    check_single_error(
        result,
        "expected expression after ','",
        4,
        5);

    std::ostringstream output;
    tpp::render_diagnostics(
        output,
        result.diagnostics.diagnostics(),
        result.sources);

    TPP_CHECK_EQ(
        output.str(),
        std::string{
            "expression.tpp:1:5: error: expected expression after ','\n"
            "  1 | f(a,)\n"
            "    |     ^\n"});
}

void lexer_and_parser_canonicalize_logical_spellings()
{
    const ParsingResult textual{"a and not b or c"};
    const ParsingResult symbolic{"a && !b || c"};

    const auto& textual_or = require_binary(
        require_success(textual),
        BinaryOperator::logical_or);
    const auto& symbolic_or = require_binary(
        require_success(symbolic),
        BinaryOperator::logical_or);

    const auto& textual_and =
        require_binary(
            *textual_or.left,
            BinaryOperator::logical_and);
    const auto& symbolic_and =
        require_binary(
            *symbolic_or.left,
            BinaryOperator::logical_and);

    require_identifier(*textual_and.left, "a");
    require_identifier(*symbolic_and.left, "a");
    require_unary(
        *textual_and.right,
        UnaryOperator::logical_not);
    require_unary(
        *symbolic_and.right,
        UnaryOperator::logical_not);
    require_identifier(*textual_or.right, "c");
    require_identifier(*symbolic_or.right, "c");
}

void lexer_to_parser_preserves_comments_literals_and_large_integers()
{
    const ParsingResult result{
        "f(\"a\\0b\", '\\n', // comment\n"
        "  -9223372036854775808)"};
    const auto& call =
        require_node<CallExpression>(require_success(result));
    TPP_CHECK_EQ(call.arguments.size(), std::size_t{3});

    const auto& string =
        require_node<StringLiteralExpression>(*call.arguments[0]);
    TPP_CHECK_EQ(string.value, std::string("a\0b", 3));

    const auto& character =
        require_node<CharacterLiteralExpression>(*call.arguments[1]);
    TPP_CHECK_EQ(character.value, '\n');

    const auto& unary =
        require_unary(*call.arguments[2], UnaryOperator::minus);
    const auto& integer =
        require_node<IntegerLiteralExpression>(*unary.operand);
    TPP_CHECK_EQ(
        integer.lexeme,
        std::string{"9223372036854775808"});
}

void parser_requires_a_trailing_eof_token()
{
    SourceManager sources;
    DiagnosticEngine diagnostics;
    const auto source = sources.add_source("invalid-unit.tpp", "x");
    const std::vector<Token> tokens{
        Token{
            .kind = TokenKind::identifier,
            .span = SourceSpan{
                .source = source,
                .begin = 0,
                .end = 1,
            },
            .value = std::monostate{},
        },
    };

    bool threw = false;
    try {
        ExpressionParser parser{tokens, sources, diagnostics};
        static_cast<void>(parser);
    } catch (const std::invalid_argument&) {
        threw = true;
    }

    TPP_CHECK(threw);
    TPP_CHECK(!diagnostics.has_errors());

    const std::vector<Token> early_eof_tokens{
        Token{
            .kind = TokenKind::end_of_file,
            .span = SourceSpan{source, 0, 0},
            .value = std::monostate{},
        },
        Token{
            .kind = TokenKind::identifier,
            .span = SourceSpan{source, 0, 1},
            .value = std::monostate{},
        },
        Token{
            .kind = TokenKind::end_of_file,
            .span = SourceSpan{source, 1, 1},
            .value = std::monostate{},
        },
    };

    threw = false;
    try {
        ExpressionParser parser{
            early_eof_tokens,
            sources,
            diagnostics};
        static_cast<void>(parser);
    } catch (const std::invalid_argument&) {
        threw = true;
    }

    TPP_CHECK(threw);
    TPP_CHECK(!diagnostics.has_errors());
}

}

int main()
{
    return tpp::test::run({
        {"manual parser unit",
         manually_constructed_tokens_are_parsed_without_the_lexer},
        {"literal and identifier nodes",
         literal_and_identifier_nodes_preserve_owned_values},
        {"large integer lexemes",
         integer_literals_are_not_converted_to_signed_values},
        {"owned AST values",
         ast_values_outlive_lexer_and_source_manager},
        {"unary operator mappings",
         unary_operator_spellings_map_to_typed_operators},
        {"binary operator mappings",
         binary_operator_spellings_map_to_typed_operators},
        {"precedence", precedence_matches_the_ebnf_levels},
        {"left associativity",
         repeated_binary_levels_are_left_associative},
        {"right associative unary",
         unary_expressions_are_right_associative},
        {"explicit parentheses",
         parentheses_are_explicit_and_override_precedence},
        {"calls and argument lists",
         calls_support_arguments_and_repeated_suffixes},
        {"chained postfix spans",
         chained_postfix_nodes_have_full_spans},
        {"chained indexing",
         indexing_suffixes_chain_left_to_right},
        {"vector constructions",
         vector_constructions_cover_types_arguments_and_postfix},
        {"nested SourceSpan and SourceId",
         nested_spans_preserve_the_selected_source_id},
        {"equality and comparison cardinality",
         equality_and_comparison_follow_ebnf_cardinality},
        {"non-expression operators",
         assignment_range_and_trailing_tokens_are_rejected},
        {"expected expression diagnostics",
         missing_and_unexpected_primaries_have_precise_errors},
        {"missing delimiter diagnostics",
         missing_closing_delimiters_use_insertion_spans},
        {"argument-list diagnostics",
         malformed_argument_lists_have_contextual_diagnostics},
        {"postfix diagnostics",
         malformed_postfix_suffixes_have_contextual_diagnostics},
        {"vector diagnostics",
         malformed_vector_types_have_contextual_diagnostics},
        {"single-error recovery",
         recovery_stops_after_one_error_and_does_not_pass_eof},
        {"stable rendered diagnostic",
         rendered_syntax_diagnostic_format_is_stable},
        {"logical spelling integration",
         lexer_and_parser_canonicalize_logical_spellings},
        {"literal Lexer to Parser integration",
         lexer_to_parser_preserves_comments_literals_and_large_integers},
        {"parser EOF precondition",
         parser_requires_a_trailing_eof_token},
    });
}

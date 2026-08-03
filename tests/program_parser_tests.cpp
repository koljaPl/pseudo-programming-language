#include "test_support.hpp"

#include "pseudo/ast/program.hpp"
#include "pseudo/diagnostics/diagnostic_engine.hpp"
#include "pseudo/lexer/lexer.hpp"
#include "pseudo/lexer/token.hpp"
#include "pseudo/parser/parser.hpp"
#include "pseudo/source/source_manager.hpp"

#include <array>
#include <cstddef>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

using tpp::AssignmentOperator;
using tpp::AssignmentStatement;
using tpp::BinaryExpression;
using tpp::BlockItem;
using tpp::BlockStatement;
using tpp::BreakStatement;
using tpp::CallExpression;
using tpp::ContinueStatement;
using tpp::DiagnosticEngine;
using tpp::Expression;
using tpp::ExpressionStatement;
using tpp::ForEachStatement;
using tpp::ForRangeStatement;
using tpp::FunctionDeclaration;
using tpp::IdentifierExpression;
using tpp::IfStatement;
using tpp::IndexExpression;
using tpp::IntegerLiteralExpression;
using tpp::Lexer;
using tpp::Parameter;
using tpp::Parser;
using tpp::Program;
using tpp::RangeOperator;
using tpp::ReturnStatement;
using tpp::ScalarTypeKind;
using tpp::SourceId;
using tpp::SourceManager;
using tpp::SourceSpan;
using tpp::Statement;
using tpp::StringLiteralExpression;
using tpp::Token;
using tpp::TokenKind;
using tpp::ValueType;
using tpp::VariableDeclaration;
using tpp::VectorConstructionExpression;
using tpp::VectorType;
using tpp::VoidType;
using tpp::WhileStatement;

class ParsingResult {
public:
    explicit ParsingResult(
        std::string contents,
        std::string display_name = "program.tpp",
        const bool use_second_source = false)
        : program{SourceSpan{SourceId{0}, 0, 0}, {}}
    {
        if (use_second_source) {
            static_cast<void>(sources.add_source("unused.tpp", "unused"));
        }

        source = sources.add_source(
            std::move(display_name),
            std::move(contents));
        Lexer lexer{source, sources, diagnostics};
        tokens = lexer.lex();
        TPP_CHECK(!diagnostics.has_errors());

        Parser parser{tokens, sources, diagnostics};
        program = parser.parse_program();
    }

    SourceManager sources;
    DiagnosticEngine diagnostics;
    SourceId source{0};
    std::vector<Token> tokens;
    Program program;
};

template <typename Node, typename Variant>
const Node& require_variant(const Variant& variant)
{
    const auto* node = std::get_if<Node>(&variant);
    TPP_CHECK(node != nullptr);
    return *node;
}

const FunctionDeclaration& require_function(
    const tpp::TopLevelDeclaration& declaration)
{
    return require_variant<FunctionDeclaration>(declaration);
}

const VariableDeclaration& require_variable(
    const tpp::TopLevelDeclaration& declaration)
{
    return require_variant<VariableDeclaration>(declaration);
}

const Statement& require_statement(const BlockItem& item)
{
    return require_variant<Statement>(item);
}

const FunctionDeclaration& require_nested_function(const BlockItem& item)
{
    return require_variant<FunctionDeclaration>(item);
}

template <typename Node>
const Node& require_statement_node(const BlockItem& item)
{
    return require_variant<Node>(require_statement(item).node);
}

template <typename Node>
const Node& require_expression_node(const Expression& expression)
{
    return require_variant<Node>(expression.node);
}

const ValueType& require_value_return_type(
    const FunctionDeclaration& function)
{
    return require_variant<ValueType>(function.return_type.node);
}

void require_scalar_type(
    const ValueType& type,
    const ScalarTypeKind expected)
{
    TPP_CHECK_EQ(require_variant<ScalarTypeKind>(type.node), expected);
}

const ValueType& require_vector_element(const ValueType& type)
{
    const auto& vector = require_variant<VectorType>(type.node);
    TPP_CHECK(vector.element_type != nullptr);
    return *vector.element_type;
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

void check_source_text(
    const ParsingResult& result,
    const SourceSpan span,
    const std::string_view expected)
{
    TPP_CHECK(span.source == result.source);
    TPP_CHECK_EQ(result.sources.slice(span), expected);
}

const tpp::Diagnostic& require_single_error(const ParsingResult& result)
{
    TPP_CHECK(result.diagnostics.has_errors());
    TPP_CHECK_EQ(result.diagnostics.error_count(), std::size_t{1});
    const auto diagnostics = result.diagnostics.diagnostics();
    TPP_CHECK_EQ(diagnostics.size(), std::size_t{1});
    return diagnostics.front();
}

void empty_program_and_source_spans_are_well_formed()
{
    const ParsingResult empty{""};
    TPP_CHECK(!empty.diagnostics.has_errors());
    TPP_CHECK(empty.program.declarations.empty());
    check_span(empty.program.span, empty.source, 0, 0);

    const ParsingResult whitespace{" \r\n\t", "second.tpp", true};
    TPP_CHECK_EQ(whitespace.source.value, std::size_t{1});
    TPP_CHECK(!whitespace.diagnostics.has_errors());
    TPP_CHECK(whitespace.program.declarations.empty());
    check_span(whitespace.program.span, whitespace.source, 0, 4);

    const ParsingResult selected_source{
        "int value;",
        "selected.tpp",
        true};
    TPP_CHECK(!selected_source.diagnostics.has_errors());
    const auto& variable = require_variable(
        selected_source.program.declarations.front());
    check_span(
        selected_source.program.span,
        selected_source.source,
        0,
        10);
    check_span(variable.span, selected_source.source, 0, 10);
    check_span(variable.type.span, selected_source.source, 0, 3);
    check_span(variable.name_span, selected_source.source, 4, 9);
}

void scalar_global_declarations_cover_all_value_types()
{
    struct Case {
        std::string_view spelling;
        ScalarTypeKind kind;
    };

    constexpr std::array cases{
        Case{"int", ScalarTypeKind::integer},
        Case{"bool", ScalarTypeKind::boolean},
        Case{"char", ScalarTypeKind::character},
        Case{"string", ScalarTypeKind::string},
    };

    for (const auto& test_case : cases) {
        const auto source = std::string{test_case.spelling} + " value;";
        const ParsingResult result{source};
        TPP_CHECK(!result.diagnostics.has_errors());
        TPP_CHECK_EQ(result.program.declarations.size(), std::size_t{1});

        const auto& variable = require_variable(
            result.program.declarations.front());
        require_scalar_type(variable.type, test_case.kind);
        TPP_CHECK_EQ(variable.name, std::string{"value"});
        TPP_CHECK(variable.initializer == nullptr);
        check_span(variable.span, result.source, 0, source.size());
        check_source_text(result, variable.name_span, "value");
    }
}

void recursive_types_and_vector_constructions_share_one_model()
{
    constexpr std::string_view source =
        "vector<vector<string>> values = "
        "vector<vector<string>>(count);";
    const ParsingResult result{std::string{source}};
    TPP_CHECK(!result.diagnostics.has_errors());

    const auto& variable = require_variable(
        result.program.declarations.front());
    const auto& declaration_inner = require_vector_element(variable.type);
    const auto& declaration_scalar =
        require_vector_element(declaration_inner);
    require_scalar_type(declaration_scalar, ScalarTypeKind::string);
    check_source_text(result, variable.type.span, "vector<vector<string>>");
    check_source_text(result, declaration_inner.span, "vector<string>");
    check_source_text(result, declaration_scalar.span, "string");

    TPP_CHECK(variable.initializer != nullptr);
    const auto& construction =
        require_expression_node<VectorConstructionExpression>(
            *variable.initializer);
    const auto& construction_inner = require_vector_element(construction.type);
    const auto& construction_scalar =
        require_vector_element(construction_inner);
    require_scalar_type(construction_scalar, ScalarTypeKind::string);
    TPP_CHECK_EQ(construction.arguments.size(), std::size_t{1});
    TPP_CHECK_EQ(
        require_expression_node<IdentifierExpression>(
            *construction.arguments.front())
            .name,
        std::string{"count"});
    check_source_text(
        result,
        construction.type.span,
        "vector<vector<string>>");
    check_source_text(result, construction_inner.span, "vector<string>");
    check_source_text(result, construction_scalar.span, "string");
    check_span(variable.span, result.source, 0, source.size());
}

void functions_parameters_locals_and_nested_functions_form_owned_ast()
{
    constexpr std::string_view source =
        "vector<int> outer(int value, vector<char> bytes) {"
        "bool ready = true;"
        "void nested(string name) { return; }"
        "return vector<int>(value);"
        "}";
    const ParsingResult result{std::string{source}};
    TPP_CHECK(!result.diagnostics.has_errors());

    const auto& outer = require_function(result.program.declarations.front());
    TPP_CHECK_EQ(outer.name, std::string{"outer"});
    require_scalar_type(
        require_vector_element(require_value_return_type(outer)),
        ScalarTypeKind::integer);
    TPP_CHECK_EQ(outer.parameters.size(), std::size_t{2});

    const Parameter& value = outer.parameters[0];
    require_scalar_type(value.type, ScalarTypeKind::integer);
    TPP_CHECK_EQ(value.name, std::string{"value"});
    check_source_text(result, value.span, "int value");

    const Parameter& bytes = outer.parameters[1];
    require_scalar_type(
        require_vector_element(bytes.type),
        ScalarTypeKind::character);
    TPP_CHECK_EQ(bytes.name, std::string{"bytes"});
    check_source_text(result, bytes.span, "vector<char> bytes");

    TPP_CHECK(outer.body != nullptr);
    TPP_CHECK_EQ(outer.body->items.size(), std::size_t{3});
    const auto& local = require_statement_node<VariableDeclaration>(
        outer.body->items[0]);
    require_scalar_type(local.type, ScalarTypeKind::boolean);
    TPP_CHECK_EQ(local.name, std::string{"ready"});
    TPP_CHECK(local.initializer != nullptr);

    const auto& nested = require_nested_function(outer.body->items[1]);
    TPP_CHECK_EQ(nested.name, std::string{"nested"});
    require_variant<VoidType>(nested.return_type.node);
    TPP_CHECK_EQ(nested.parameters.size(), std::size_t{1});
    TPP_CHECK_EQ(nested.parameters.front().name, std::string{"name"});
    TPP_CHECK_EQ(nested.body->items.size(), std::size_t{1});
    const auto& empty_return = require_statement_node<ReturnStatement>(
        nested.body->items.front());
    TPP_CHECK(empty_return.value == nullptr);

    const auto& value_return = require_statement_node<ReturnStatement>(
        outer.body->items[2]);
    TPP_CHECK(value_return.value != nullptr);
    require_expression_node<VectorConstructionExpression>(*value_return.value);
    check_span(outer.span, result.source, 0, source.size());
    check_source_text(result, outer.body->span, source.substr(source.find('{')));
}

void function_return_types_and_spans_are_typed()
{
    constexpr std::string_view source =
        "int value(){return 0;}void action(){return;}";
    const ParsingResult result{std::string{source}};
    TPP_CHECK(!result.diagnostics.has_errors());
    TPP_CHECK_EQ(result.program.declarations.size(), std::size_t{2});

    const auto& value = require_function(result.program.declarations[0]);
    require_scalar_type(
        require_value_return_type(value),
        ScalarTypeKind::integer);
    check_source_text(result, value.return_type.span, "int");
    check_source_text(result, value.span, "int value(){return 0;}");

    const auto& action = require_function(result.program.declarations[1]);
    require_variant<VoidType>(action.return_type.node);
    check_source_text(result, action.return_type.span, "void");
    check_source_text(result, action.span, "void action(){return;}");
}

void vector_declaration_construction_function_and_block_are_disambiguated()
{
    constexpr std::string_view source =
        "void test() {"
        "vector<int> values;"
        "vector<int>(n);"
        "vector<int> make() { return vector<int>(); }"
        "{ string text; }"
        "foo(a)[i].length();"
        "}";
    const ParsingResult result{std::string{source}};
    TPP_CHECK(!result.diagnostics.has_errors());

    const auto& function = require_function(result.program.declarations.front());
    TPP_CHECK_EQ(function.body->items.size(), std::size_t{5});

    const auto& variable = require_statement_node<VariableDeclaration>(
        function.body->items[0]);
    TPP_CHECK_EQ(variable.name, std::string{"values"});

    const auto& construction_statement =
        require_statement_node<ExpressionStatement>(function.body->items[1]);
    require_expression_node<VectorConstructionExpression>(
        *construction_statement.expression);

    const auto& nested = require_nested_function(function.body->items[2]);
    TPP_CHECK_EQ(nested.name, std::string{"make"});
    require_scalar_type(
        require_vector_element(require_value_return_type(nested)),
        ScalarTypeKind::integer);

    const auto& block_statement = require_statement_node<BlockStatement>(
        function.body->items[3]);
    TPP_CHECK(block_statement.block != nullptr);
    TPP_CHECK_EQ(block_statement.block->items.size(), std::size_t{1});
    require_statement_node<VariableDeclaration>(
        block_statement.block->items.front());

    const auto& expression_statement =
        require_statement_node<ExpressionStatement>(function.body->items[4]);
    require_expression_node<CallExpression>(*expression_statement.expression);
    check_source_text(
        result,
        require_statement(function.body->items[4]).span,
        "foo(a)[i].length();");
}

void all_assignment_operators_and_index_targets_are_typed()
{
    constexpr std::string_view source =
        "void f(){"
        "x=0;x+=1;x-=2;x*=3;x/=4;x%=5;"
        "matrix[i][j]=value;"
        "}";
    const ParsingResult result{std::string{source}};
    TPP_CHECK(!result.diagnostics.has_errors());
    const auto& items =
        require_function(result.program.declarations.front()).body->items;
    TPP_CHECK_EQ(items.size(), std::size_t{7});

    constexpr std::array operators{
        AssignmentOperator::assign,
        AssignmentOperator::add_assign,
        AssignmentOperator::subtract_assign,
        AssignmentOperator::multiply_assign,
        AssignmentOperator::divide_assign,
        AssignmentOperator::remainder_assign,
    };
    constexpr std::array statement_texts{
        std::string_view{"x=0;"},
        std::string_view{"x+=1;"},
        std::string_view{"x-=2;"},
        std::string_view{"x*=3;"},
        std::string_view{"x/=4;"},
        std::string_view{"x%=5;"},
    };

    for (std::size_t index = 0; index < operators.size(); ++index) {
        const auto& assignment =
            require_statement_node<AssignmentStatement>(items[index]);
        TPP_CHECK_EQ(assignment.operator_kind, operators[index]);
        TPP_CHECK_EQ(assignment.target.name, std::string{"x"});
        TPP_CHECK(assignment.target.indices.empty());
        TPP_CHECK(assignment.value != nullptr);
        require_expression_node<IntegerLiteralExpression>(*assignment.value);
        check_source_text(
            result,
            require_statement(items[index]).span,
            statement_texts[index]);
    }

    const auto& indexed =
        require_statement_node<AssignmentStatement>(items.back());
    TPP_CHECK_EQ(indexed.target.name, std::string{"matrix"});
    TPP_CHECK_EQ(indexed.target.indices.size(), std::size_t{2});
    TPP_CHECK_EQ(
        require_expression_node<IdentifierExpression>(
            *indexed.target.indices[0])
            .name,
        std::string{"i"});
    TPP_CHECK_EQ(
        require_expression_node<IdentifierExpression>(
            *indexed.target.indices[1])
            .name,
        std::string{"j"});
    check_source_text(result, indexed.target.span, "matrix[i][j]");
    check_source_text(result, indexed.target.name_span, "matrix");
    check_source_text(
        result,
        require_statement(items.back()).span,
        "matrix[i][j]=value;");
}

void if_while_blocks_and_jump_statements_preserve_structure()
{
    constexpr std::string_view source =
        "void f(){"
        "if condition { break; } else { continue; }"
        "if ready {}"
        "while running { { return; } }"
        "}";
    const ParsingResult result{std::string{source}};
    TPP_CHECK(!result.diagnostics.has_errors());
    const auto& body =
        *require_function(result.program.declarations.front()).body;
    TPP_CHECK_EQ(body.items.size(), std::size_t{3});

    const auto& if_statement =
        require_statement_node<IfStatement>(body.items[0]);
    TPP_CHECK_EQ(
        require_expression_node<IdentifierExpression>(
            *if_statement.condition)
            .name,
        std::string{"condition"});
    TPP_CHECK(if_statement.then_block != nullptr);
    TPP_CHECK(if_statement.else_block != nullptr);
    require_statement_node<BreakStatement>(
        if_statement.then_block->items.front());
    require_statement_node<ContinueStatement>(
        if_statement.else_block->items.front());
    check_source_text(
        result,
        require_statement(if_statement.then_block->items.front()).span,
        "break;");
    check_source_text(
        result,
        require_statement(if_statement.else_block->items.front()).span,
        "continue;");

    const auto& if_without_else =
        require_statement_node<IfStatement>(body.items[1]);
    TPP_CHECK(if_without_else.then_block != nullptr);
    TPP_CHECK(if_without_else.else_block == nullptr);
    check_source_text(
        result,
        require_statement(body.items[1]).span,
        "if ready {}");

    const auto& while_statement =
        require_statement_node<WhileStatement>(body.items[2]);
    TPP_CHECK(while_statement.body != nullptr);
    const auto& nested_block = require_statement_node<BlockStatement>(
        while_statement.body->items.front());
    const auto& return_statement = require_statement_node<ReturnStatement>(
        nested_block.block->items.front());
    TPP_CHECK(return_statement.value == nullptr);
    check_source_text(
        result,
        require_statement(body.items[2]).span,
        "while running { { return; } }");
    check_source_text(
        result,
        require_statement(while_statement.body->items.front()).span,
        "{ return; }");
    check_source_text(
        result,
        require_statement(body.items[0]).span,
        "if condition { break; } else { continue; }");
}

void foreach_and_both_range_operators_have_distinct_nodes()
{
    constexpr std::string_view source =
        "void f(){"
        "for value in values { continue; }"
        "for i in begin+1..=end-1 { break; }"
        "for j in 0..n { return; }"
        "}";
    const ParsingResult result{std::string{source}};
    TPP_CHECK(!result.diagnostics.has_errors());
    const auto& items =
        require_function(result.program.declarations.front()).body->items;
    TPP_CHECK_EQ(items.size(), std::size_t{3});

    const auto& each = require_statement_node<ForEachStatement>(items[0]);
    TPP_CHECK_EQ(each.variable, std::string{"value"});
    TPP_CHECK_EQ(
        require_expression_node<IdentifierExpression>(*each.iterable).name,
        std::string{"values"});
    require_statement_node<ContinueStatement>(each.body->items.front());
    check_source_text(
        result,
        require_statement(items[0]).span,
        "for value in values { continue; }");

    const auto& inclusive =
        require_statement_node<ForRangeStatement>(items[1]);
    TPP_CHECK_EQ(inclusive.variable, std::string{"i"});
    TPP_CHECK_EQ(inclusive.operator_kind, RangeOperator::inclusive);
    require_expression_node<BinaryExpression>(*inclusive.begin);
    require_expression_node<BinaryExpression>(*inclusive.end);
    require_statement_node<BreakStatement>(inclusive.body->items.front());
    check_source_text(
        result,
        require_statement(items[1]).span,
        "for i in begin+1..=end-1 { break; }");

    const auto& exclusive =
        require_statement_node<ForRangeStatement>(items[2]);
    TPP_CHECK_EQ(exclusive.variable, std::string{"j"});
    TPP_CHECK_EQ(exclusive.operator_kind, RangeOperator::exclusive);
    TPP_CHECK_EQ(
        require_expression_node<IntegerLiteralExpression>(*exclusive.begin)
            .lexeme,
        std::string{"0"});
    TPP_CHECK_EQ(
        require_expression_node<IdentifierExpression>(*exclusive.end).name,
        std::string{"n"});
    check_source_text(result, exclusive.variable_span, "j");
    check_source_text(
        result,
        require_statement(items[2]).span,
        "for j in 0..n { return; }");
}

void invalid_assignment_targets_are_rejected_and_recovered()
{
    constexpr std::array invalid_targets{
        std::string_view{"f() = 1;"},
        std::string_view{"object.member = 1;"},
        std::string_view{"(value) = 1;"},
        std::string_view{"items[i].member = 1;"},
    };

    for (const auto target : invalid_targets) {
        const ParsingResult result{
            std::string{"void f(){"} + std::string{target}
            + " int good;}"};
        const auto& diagnostic = require_single_error(result);
        TPP_CHECK_EQ(
            diagnostic.message,
            std::string{
                "invalid assignment target; expected identifier or indexing chain"});

        const auto& body =
            *require_function(result.program.declarations.front()).body;
        TPP_CHECK_EQ(body.items.size(), std::size_t{1});
        TPP_CHECK_EQ(
            require_statement_node<VariableDeclaration>(body.items.front())
                .name,
            std::string{"good"});
    }
}

void malformed_constructs_have_contextual_diagnostics()
{
    struct Case {
        std::string_view source;
        std::string_view message;
    };

    constexpr std::array cases{
        Case{"int ;", "expected declaration name after type"},
        Case{"vector<int> ;", "expected declaration name after type"},
        Case{"vector<void> value;", "expected value type in vector type"},
        Case{"void value;", "expected '(' after function name"},
        Case{"int f( {}", "expected ')' after parameter list"},
        Case{"int f(int value {}", "expected ')' after parameter list"},
        Case{"int f(int) {}", "expected parameter name after type"},
        Case{"int f(value) {}", "expected parameter type"},
        Case{"int f(void value) {}", "expected parameter type"},
        Case{"int f(int value,) {}", "expected parameter after ','"},
        Case{"int f(int a int b) {}", "expected ',' or ')' after parameter"},
        Case{"int f()", "expected function body"},
        Case{"if true {}", "expected top-level declaration"},
        Case{"void f(){if {}}", "expected expression"},
        Case{"void f(){if true return;} ", "expected block after if condition"},
        Case{"void f(){if true {} else return;} ", "expected block after 'else'"},
        Case{"void f(){while true return;} ", "expected block after while condition"},
        Case{"void f(){for in values {}}", "expected loop variable after 'for'"},
        Case{"void f(){for value values {}}", "expected 'in' after loop variable"},
        Case{"void f(){for value in {}}", "expected expression"},
        Case{"void f(){for i in 0.. {}}", "expected expression"},
        Case{"void f(){for value in values return;} ",
             "expected range operator or block after for expression"},
        Case{"void f(){return 1}", "expected ';' after return statement"},
        Case{"void f(){break}", "expected ';' after break statement"},
        Case{"void f(){continue}", "expected ';' after continue statement"},
        Case{"void f(){int value}", "expected ';' after variable declaration"},
        Case{"void f(){value=1}", "expected ';' after assignment"},
        Case{"void f(){value}", "expected ';' after expression"},
        Case{"void f(){return;", "expected '}' to close block"},
    };

    for (const auto& test_case : cases) {
        const ParsingResult result{std::string{test_case.source}};
        const auto& diagnostic = require_single_error(result);
        TPP_CHECK_EQ(diagnostic.message, test_case.message);
        TPP_CHECK(diagnostic.primary_span.has_value());
        TPP_CHECK(diagnostic.primary_span->source == result.source);
    }
}

void missing_tokens_use_zero_length_insertion_spans()
{
    const ParsingResult missing_parenthesis{"int f( {}"};
    const auto& parenthesis_diagnostic =
        require_single_error(missing_parenthesis);
    TPP_CHECK_EQ(
        parenthesis_diagnostic.message,
        std::string{"expected ')' after parameter list"});
    check_span(
        *parenthesis_diagnostic.primary_span,
        missing_parenthesis.source,
        7,
        7);

    const ParsingResult missing_semicolon{"void f(){return 1}"};
    const auto& semicolon_diagnostic =
        require_single_error(missing_semicolon);
    TPP_CHECK_EQ(
        semicolon_diagnostic.message,
        std::string{"expected ';' after return statement"});
    check_span(
        *semicolon_diagnostic.primary_span,
        missing_semicolon.source,
        17,
        17);

    const ParsingResult missing_brace{"void f(){return;"};
    const auto& brace_diagnostic = require_single_error(missing_brace);
    TPP_CHECK_EQ(
        brace_diagnostic.message,
        std::string{"expected '}' to close block"});
    check_span(
        *brace_diagnostic.primary_span,
        missing_brace.source,
        16,
        16);
}

void recovery_reports_independent_errors_and_keeps_valid_constructs()
{
    constexpr std::string_view source =
        "int first = ;"
        "int good = 1;"
        "void f(){"
        "int local = ;"
        "break "
        "continue "
        "return;"
        "}"
        "string last = ;";
    const ParsingResult result{std::string{source}};

    TPP_CHECK(result.diagnostics.has_errors());
    TPP_CHECK_EQ(result.diagnostics.error_count(), std::size_t{5});
    const auto diagnostics = result.diagnostics.diagnostics();
    TPP_CHECK_EQ(diagnostics.size(), std::size_t{5});
    TPP_CHECK_EQ(diagnostics[0].message, std::string{"expected expression"});
    TPP_CHECK_EQ(diagnostics[1].message, std::string{"expected expression"});
    TPP_CHECK_EQ(
        diagnostics[2].message,
        std::string{"expected ';' after break statement"});
    TPP_CHECK_EQ(
        diagnostics[3].message,
        std::string{"expected ';' after continue statement"});
    TPP_CHECK_EQ(diagnostics[4].message, std::string{"expected expression"});

    TPP_CHECK_EQ(result.program.declarations.size(), std::size_t{2});
    TPP_CHECK_EQ(
        require_variable(result.program.declarations[0]).name,
        std::string{"good"});
    const auto& function = require_function(result.program.declarations[1]);
    TPP_CHECK_EQ(function.body->items.size(), std::size_t{3});
    require_statement_node<BreakStatement>(function.body->items[0]);
    require_statement_node<ContinueStatement>(function.body->items[1]);
    require_statement_node<ReturnStatement>(function.body->items[2]);
}

void malformed_function_recovery_reaches_the_next_declaration()
{
    const ParsingResult result{"int bad(int,) {} int good;"};
    const auto& diagnostic = require_single_error(result);
    TPP_CHECK_EQ(
        diagnostic.message,
        std::string{"expected parameter name after type"});
    TPP_CHECK_EQ(result.program.declarations.size(), std::size_t{1});
    TPP_CHECK_EQ(
        require_variable(result.program.declarations.front()).name,
        std::string{"good"});

    const ParsingResult nested{
        "void outer(){ int bad(int,) {} broken = ; int good; }"};
    TPP_CHECK_EQ(nested.diagnostics.error_count(), std::size_t{2});
    TPP_CHECK_EQ(
        nested.diagnostics.diagnostics()[0].message,
        std::string{"expected parameter name after type"});
    TPP_CHECK_EQ(
        nested.diagnostics.diagnostics()[1].message,
        std::string{"expected expression"});
    const auto& body =
        *require_function(nested.program.declarations.front()).body;
    TPP_CHECK_EQ(body.items.size(), std::size_t{1});
    TPP_CHECK_EQ(
        require_statement_node<VariableDeclaration>(body.items.front()).name,
        std::string{"good"});
}

void stray_tokens_make_progress_and_preserve_following_declarations()
{
    const ParsingResult top_level{"; } int good;"};
    TPP_CHECK_EQ(
        top_level.diagnostics.error_count(),
        std::size_t{2});
    TPP_CHECK_EQ(top_level.program.declarations.size(), std::size_t{1});
    TPP_CHECK_EQ(
        require_variable(top_level.program.declarations.front()).name,
        std::string{"good"});

    const ParsingResult block{"void f(){;;; int good;}"};
    TPP_CHECK_EQ(block.diagnostics.error_count(), std::size_t{3});
    const auto& body =
        *require_function(block.program.declarations.front()).body;
    TPP_CHECK_EQ(body.items.size(), std::size_t{1});
    TPP_CHECK_EQ(
        require_statement_node<VariableDeclaration>(body.items.front()).name,
        std::string{"good"});
}

void syntax_diagnostic_rendering_is_stable()
{
    const ParsingResult result{"int value = ;"};
    const auto& diagnostic = require_single_error(result);
    TPP_CHECK_EQ(diagnostic.message, std::string{"expected expression"});
    TPP_CHECK(diagnostic.primary_span.has_value());
    check_span(*diagnostic.primary_span, result.source, 12, 13);

    std::ostringstream output;
    tpp::render_diagnostics(
        output,
        result.diagnostics.diagnostics(),
        result.sources);
    TPP_CHECK_EQ(
        output.str(),
        std::string{
            "program.tpp:1:13: error: expected expression\n"
            "  1 | int value = ;\n"
            "    |             ^\n"});
}

Program parse_detached_program()
{
    ParsingResult result{
        "string global = \"owned\";"
        "void outer(int parameter){"
        "for item in values { return; }"
        "}"};
    TPP_CHECK(!result.diagnostics.has_errors());
    return std::move(result.program);
}

void program_ast_owns_names_and_values_after_sources_are_destroyed()
{
    const auto program = parse_detached_program();
    TPP_CHECK_EQ(program.declarations.size(), std::size_t{2});

    const auto& global = require_variable(program.declarations[0]);
    TPP_CHECK_EQ(global.name, std::string{"global"});
    TPP_CHECK_EQ(
        require_expression_node<StringLiteralExpression>(*global.initializer)
            .value,
        std::string{"owned"});

    const auto& function = require_function(program.declarations[1]);
    TPP_CHECK_EQ(function.name, std::string{"outer"});
    TPP_CHECK_EQ(
        function.parameters.front().name,
        std::string{"parameter"});
    const auto& loop =
        require_statement_node<ForEachStatement>(function.body->items.front());
    TPP_CHECK_EQ(loop.variable, std::string{"item"});
    TPP_CHECK_EQ(
        require_expression_node<IdentifierExpression>(*loop.iterable).name,
        std::string{"values"});
}

void parser_requires_one_trailing_eof_token()
{
    SourceManager sources;
    DiagnosticEngine diagnostics;
    const auto source = sources.add_source("unit.tpp", "int");
    const std::vector<Token> tokens{
        Token{
            .kind = TokenKind::keyword_int,
            .span = SourceSpan{source, 0, 3},
            .value = std::monostate{},
        },
    };

    bool threw = false;
    try {
        Parser parser{tokens, sources, diagnostics};
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
            .kind = TokenKind::keyword_int,
            .span = SourceSpan{source, 0, 3},
            .value = std::monostate{},
        },
        Token{
            .kind = TokenKind::end_of_file,
            .span = SourceSpan{source, 3, 3},
            .value = std::monostate{},
        },
    };

    threw = false;
    try {
        Parser parser{early_eof_tokens, sources, diagnostics};
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
        {"empty program and SourceSpan",
         empty_program_and_source_spans_are_well_formed},
        {"scalar global declarations",
         scalar_global_declarations_cover_all_value_types},
        {"recursive shared type model",
         recursive_types_and_vector_constructions_share_one_model},
        {"functions parameters locals nested",
         functions_parameters_locals_and_nested_functions_form_owned_ast},
        {"function return types and spans",
         function_return_types_and_spans_are_typed},
        {"declaration expression ambiguity",
         vector_declaration_construction_function_and_block_are_disambiguated},
        {"assignment operators and targets",
         all_assignment_operators_and_index_targets_are_typed},
        {"if while blocks and jumps",
         if_while_blocks_and_jump_statements_preserve_structure},
        {"foreach and ranges",
         foreach_and_both_range_operators_have_distinct_nodes},
        {"invalid assignment targets",
         invalid_assignment_targets_are_rejected_and_recovered},
        {"contextual syntax diagnostics",
         malformed_constructs_have_contextual_diagnostics},
        {"missing token insertion spans",
         missing_tokens_use_zero_length_insertion_spans},
        {"multiple-error recovery",
         recovery_reports_independent_errors_and_keeps_valid_constructs},
        {"malformed function recovery",
         malformed_function_recovery_reaches_the_next_declaration},
        {"stray token progress guard",
         stray_tokens_make_progress_and_preserve_following_declarations},
        {"stable syntax diagnostic",
         syntax_diagnostic_rendering_is_stable},
        {"owned program AST",
         program_ast_owns_names_and_values_after_sources_are_destroyed},
        {"parser EOF precondition",
         parser_requires_one_trailing_eof_token},
    });
}

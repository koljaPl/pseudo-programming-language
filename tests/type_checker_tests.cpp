#include "test_support.hpp"

#include "pseudo/ast/program.hpp"
#include "pseudo/diagnostics/diagnostic.hpp"
#include "pseudo/diagnostics/diagnostic_engine.hpp"
#include "pseudo/lexer/lexer.hpp"
#include "pseudo/parser/parser.hpp"
#include "pseudo/semantic/declaration_collector.hpp"
#include "pseudo/semantic/declaration_info.hpp"
#include "pseudo/semantic/name_resolver.hpp"
#include "pseudo/semantic/resolution_info.hpp"
#include "pseudo/semantic/symbol_table.hpp"
#include "pseudo/semantic/type_checker.hpp"
#include "pseudo/semantic/type_context.hpp"
#include "pseudo/semantic/type_info.hpp"
#include "pseudo/source/source_manager.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

using tpp::AssignmentStatement;
using tpp::AssignmentTarget;
using tpp::BinaryExpression;
using tpp::Block;
using tpp::BuiltinFunctionKind;
using tpp::CallExpression;
using tpp::DeclarationCollector;
using tpp::DeclarationInfo;
using tpp::Diagnostic;
using tpp::DiagnosticEngine;
using tpp::DiagnosticSeverity;
using tpp::Expression;
using tpp::ExpressionStatement;
using tpp::ForEachStatement;
using tpp::ForRangeStatement;
using tpp::FunctionDeclaration;
using tpp::IdentifierExpression;
using tpp::IfStatement;
using tpp::IndexExpression;
using tpp::Lexer;
using tpp::MemberAccessExpression;
using tpp::MemberKind;
using tpp::NameResolver;
using tpp::ParenthesizedExpression;
using tpp::Parser;
using tpp::Program;
using tpp::ResolutionInfo;
using tpp::ReturnStatement;
using tpp::SourceId;
using tpp::SourceManager;
using tpp::SourceSpan;
using tpp::Statement;
using tpp::SymbolId;
using tpp::SymbolTable;
using tpp::Token;
using tpp::TypeChecker;
using tpp::TypeContext;
using tpp::TypeId;
using tpp::TypeInfo;
using tpp::UnaryExpression;
using tpp::VariableDeclaration;
using tpp::VectorConstructionExpression;
using tpp::WhileStatement;

class TypeCheckResult {
public:
    explicit TypeCheckResult(std::string contents)
        : program{SourceSpan{SourceId{0}, 0, 0}, {}}
    {
        source = sources.add_source("program.tpp", std::move(contents));

        Lexer lexer{source, sources, diagnostics};
        tokens = lexer.lex();
        TPP_CHECK(!diagnostics.has_errors());

        Parser parser{tokens, sources, diagnostics};
        program = parser.parse_program();
        TPP_CHECK(!diagnostics.has_errors());

        DeclarationCollector collector{
            types,
            symbols,
            declarations,
            diagnostics,
        };
        TPP_CHECK(collector.collect(program));

        NameResolver resolver{
            symbols,
            declarations,
            resolutions,
            diagnostics,
        };
        TPP_CHECK(resolver.resolve(program));
        TPP_CHECK(!diagnostics.has_errors());
    }

    bool check()
    {
        TypeChecker checker{
            types,
            symbols,
            declarations,
            resolutions,
            type_info,
            diagnostics,
        };
        return checker.check(program);
    }

    SourceManager sources;
    DiagnosticEngine diagnostics;
    SourceId source{0};
    std::vector<Token> tokens;
    Program program;
    TypeContext types;
    SymbolTable symbols;
    DeclarationInfo declarations;
    ResolutionInfo resolutions;
    TypeInfo type_info;
};

template <typename Node, typename Variant>
const Node& require_variant(const Variant& variant)
{
    const auto* node = std::get_if<Node>(&variant);
    TPP_CHECK(node != nullptr);
    return *node;
}

template <typename Node, typename Variant>
Node& require_variant(Variant& variant)
{
    auto* node = std::get_if<Node>(&variant);
    TPP_CHECK(node != nullptr);
    return *node;
}

template <typename Node>
const Node& require_expression_node(const Expression& expression)
{
    return require_variant<Node>(expression.node);
}

template <typename Node>
Node& require_expression_node(Expression& expression)
{
    return require_variant<Node>(expression.node);
}

FunctionDeclaration& require_function(Program& program, const std::size_t index)
{
    TPP_CHECK(index < program.declarations.size());
    return require_variant<FunctionDeclaration>(program.declarations[index]);
}

const VariableDeclaration& require_global_variable(
    const Program& program,
    const std::size_t index)
{
    TPP_CHECK(index < program.declarations.size());
    return require_variant<VariableDeclaration>(program.declarations[index]);
}

Block& require_body(FunctionDeclaration& function)
{
    TPP_CHECK(function.body != nullptr);
    return *function.body;
}

const Statement& require_statement(const Block& block, const std::size_t index)
{
    TPP_CHECK(index < block.items.size());
    return require_variant<Statement>(block.items[index]);
}

Statement& require_statement(Block& block, const std::size_t index)
{
    TPP_CHECK(index < block.items.size());
    return require_variant<Statement>(block.items[index]);
}

template <typename Node>
const Node& require_statement_node(const Block& block, const std::size_t index)
{
    return require_variant<Node>(require_statement(block, index).node);
}

template <typename Node>
Node& require_statement_node(Block& block, const std::size_t index)
{
    return require_variant<Node>(require_statement(block, index).node);
}

const Expression& require_expression(const tpp::ExpressionPtr& expression)
{
    TPP_CHECK(expression != nullptr);
    return *expression;
}

Expression& require_expression(tpp::ExpressionPtr& expression)
{
    TPP_CHECK(expression != nullptr);
    return *expression;
}

const Expression& require_expression_statement(
    const Block& block,
    const std::size_t index)
{
    return require_expression(
        require_statement_node<ExpressionStatement>(block, index).expression);
}

Expression& require_expression_statement(Block& block, const std::size_t index)
{
    return require_expression(
        require_statement_node<ExpressionStatement>(block, index).expression);
}

TypeId require_type(const TypeInfo& info, const Expression& expression)
{
    const auto type = info.type_of(expression);
    TPP_CHECK(type.has_value());
    return *type;
}

TypeId require_type(const TypeInfo& info, const AssignmentTarget& target)
{
    const auto type = info.type_of(target);
    TPP_CHECK(type.has_value());
    return *type;
}

SymbolId require_symbol(const std::optional<SymbolId> symbol)
{
    TPP_CHECK(symbol.has_value());
    return *symbol;
}

void check_span(const SourceSpan actual, const SourceSpan expected)
{
    TPP_CHECK_EQ(actual.source, expected.source);
    TPP_CHECK_EQ(actual.begin, expected.begin);
    TPP_CHECK_EQ(actual.end, expected.end);
}

const Diagnostic& require_diagnostic(
    const TypeCheckResult& result,
    const std::size_t index)
{
    const auto diagnostics = result.diagnostics.diagnostics();
    TPP_CHECK(index < diagnostics.size());
    return diagnostics[index];
}

void literal_and_nested_expression_types_are_recorded()
{
    TypeCheckResult result{R"(void inspect() {
    0;
    true;
    'x';
    "text";
    1 + 2 * 3;
    (1 + 2) * 3;
    true || false && true;
})"};

    TPP_CHECK(result.check());
    TPP_CHECK(!result.type_info.empty());
    const auto& body = require_body(require_function(result.program, 0));

    TPP_CHECK_EQ(
        require_type(result.type_info, require_expression_statement(body, 0)),
        result.types.integer_type());
    TPP_CHECK_EQ(
        require_type(result.type_info, require_expression_statement(body, 1)),
        result.types.boolean_type());
    TPP_CHECK_EQ(
        require_type(result.type_info, require_expression_statement(body, 2)),
        result.types.character_type());
    TPP_CHECK_EQ(
        require_type(result.type_info, require_expression_statement(body, 3)),
        result.types.string_type());

    const auto& sum = require_expression_statement(body, 4);
    const auto& sum_node = require_expression_node<BinaryExpression>(sum);
    const auto& product = require_expression(sum_node.right);
    TPP_CHECK_EQ(require_type(result.type_info, sum), result.types.integer_type());
    TPP_CHECK_EQ(
        require_type(result.type_info, require_expression(sum_node.left)),
        result.types.integer_type());
    TPP_CHECK_EQ(
        require_type(result.type_info, product),
        result.types.integer_type());

    const auto& parenthesized_product = require_expression_statement(body, 5);
    const auto& product_node =
        require_expression_node<BinaryExpression>(parenthesized_product);
    const auto& parentheses = require_expression_node<ParenthesizedExpression>(
        require_expression(product_node.left));
    TPP_CHECK_EQ(
        require_type(result.type_info, require_expression(product_node.left)),
        result.types.integer_type());
    TPP_CHECK_EQ(
        require_type(result.type_info, require_expression(parentheses.expression)),
        result.types.integer_type());

    const auto& logical_or = require_expression_statement(body, 6);
    const auto& or_node = require_expression_node<BinaryExpression>(logical_or);
    TPP_CHECK_EQ(
        require_type(result.type_info, logical_or),
        result.types.boolean_type());
    TPP_CHECK_EQ(
        require_type(result.type_info, require_expression(or_node.right)),
        result.types.boolean_type());
}

void every_valid_unary_and_binary_operator_is_checked()
{
    TypeCheckResult result{R"(void inspect() {
    +1; -1; !true; not false;
    1 + 2; 3 - 2; 2 * 4; 8 / 2; 9 % 4;
    true && false; true and false; true || false; true or false;
    1 == 2; 1 != 2; true == false; true != false;
    'a' == 'b'; 'a' != 'b'; "a" == "b"; "a" != "b";
    1 < 2; 1 <= 2; 2 > 1; 2 >= 1;
})"};

    TPP_CHECK(result.check());
    TPP_CHECK_EQ(result.diagnostics.error_count(), std::size_t{0});
    const auto& body = require_body(require_function(result.program, 0));
    TPP_CHECK_EQ(body.items.size(), std::size_t{25});

    for (std::size_t index = 0; index < 9; ++index) {
        TPP_CHECK_EQ(
            require_type(
                result.type_info,
                require_expression_statement(body, index)),
            index == 2 || index == 3
                ? result.types.boolean_type()
                : result.types.integer_type());
    }
    for (std::size_t index = 9; index < body.items.size(); ++index) {
        TPP_CHECK_EQ(
            require_type(
                result.type_info,
                require_expression_statement(body, index)),
            result.types.boolean_type());
    }
}

void string_addition_and_ordering_have_stable_types()
{
    TypeCheckResult result{R"(void inspect() {
    "left" + "right";
    "a" < "b";
    "a" <= "b";
    "b" > "a";
    "b" >= "a";
})"};

    TPP_CHECK(result.check());
    const auto& body = require_body(require_function(result.program, 0));
    TPP_CHECK_EQ(
        require_type(result.type_info, require_expression_statement(body, 0)),
        result.types.string_type());
    for (std::size_t index = 1; index < body.items.size(); ++index) {
        TPP_CHECK_EQ(
            require_type(
                result.type_info,
                require_expression_statement(body, index)),
            result.types.boolean_type());
    }
}

void invalid_operators_report_once_per_expression_without_cascades()
{
    TypeCheckResult result{R"(void inspect() {
    +"x";
    !1;
    true + false;
    1 && 2;
    "a" + 1;
    "a" < 1;
    1 == true;
    vector<int>() == vector<int>();
    1 + (true * "x");
})"};

    TPP_CHECK(!result.check());
    TPP_CHECK_EQ(result.diagnostics.error_count(), std::size_t{9});
    const auto& body = require_body(require_function(result.program, 0));
    for (std::size_t index = 0; index < 8; ++index) {
        const auto& expression = require_expression_statement(body, index);
        TPP_CHECK(!result.type_info.type_of(expression).has_value());
        const auto& diagnostic = require_diagnostic(result, index);
        TPP_CHECK_EQ(diagnostic.severity, DiagnosticSeverity::error);
        TPP_CHECK(diagnostic.primary_span.has_value());
        check_span(*diagnostic.primary_span, expression.span);
    }

    const auto& outer = require_expression_statement(body, 8);
    const auto& outer_binary = require_expression_node<BinaryExpression>(outer);
    const auto& parentheses = require_expression_node<ParenthesizedExpression>(
        require_expression(outer_binary.right));
    const auto& invalid_inner = require_expression(parentheses.expression);
    TPP_CHECK(!result.type_info.type_of(invalid_inner).has_value());
    TPP_CHECK(!result.type_info.type_of(outer).has_value());
    const auto& last = require_diagnostic(result, 8);
    TPP_CHECK(last.primary_span.has_value());
    check_span(*last.primary_span, invalid_inner.span);
}

void initializers_assignments_and_compound_assignments_are_checked()
{
    TypeCheckResult result{R"(void inspect() {
    int number = 1;
    bool flag = true;
    char character = 'x';
    string text = "hello";
    vector<int> values = vector<int>(2, 0);
    number = 2;
    flag = false;
    character = text[0];
    text = "world";
    values[0] = number;
    text[0] = character;
    number += 1;
    number -= 1;
    number *= 2;
    number /= 2;
    number %= 2;
    text += "!";
})"};

    TPP_CHECK(result.check());
    const auto& body = require_body(require_function(result.program, 0));
    for (std::size_t index = 5; index < body.items.size(); ++index) {
        const auto& assignment =
            require_statement_node<AssignmentStatement>(body, index);
        const auto expected = index == 6
            ? result.types.boolean_type()
            : index == 7 || index == 10
                ? result.types.character_type()
                : index == 8 || index == 16
                    ? result.types.string_type()
                    : result.types.integer_type();
        TPP_CHECK_EQ(
            require_type(result.type_info, assignment.target),
            expected);
        TPP_CHECK_EQ(
            require_type(result.type_info, require_expression(assignment.value)),
            expected);
    }
}

void invalid_initializers_and_assignments_have_precise_spans()
{
    TypeCheckResult result{R"(int function() { return 0; }
void inspect() {
    int number = "x";
    vector<int> values;
    string text;
    number = true;
    values[0] = "x";
    text[0] = "x";
    text += 'x';
    number += true;
    function = 1;
})"};

    TPP_CHECK(!result.check());
    TPP_CHECK_EQ(result.diagnostics.error_count(), std::size_t{7});
    const auto& body = require_body(require_function(result.program, 1));
    const auto& number = require_statement_node<VariableDeclaration>(body, 0);
    TPP_CHECK(number.initializer != nullptr);
    const auto& first = require_diagnostic(result, 0);
    TPP_CHECK_EQ(
        first.message,
        std::string{"cannot initialize 'int' with value of type 'string'"});
    TPP_CHECK(first.primary_span.has_value());
    check_span(*first.primary_span, number.initializer->span);

    for (std::size_t diagnostic_index = 1; diagnostic_index < 6;
         ++diagnostic_index) {
        const auto& diagnostic = require_diagnostic(result, diagnostic_index);
        TPP_CHECK_EQ(diagnostic.severity, DiagnosticSeverity::error);
        TPP_CHECK(diagnostic.primary_span.has_value());
        const auto& assignment = require_statement_node<AssignmentStatement>(
            body,
            diagnostic_index + 2);
        check_span(*diagnostic.primary_span, assignment.value->span);
    }

    const auto& function_assignment =
        require_statement_node<AssignmentStatement>(body, 8);
    TPP_CHECK(!result.type_info.type_of(function_assignment.target).has_value());
    const auto& function_error = require_diagnostic(result, 6);
    TPP_CHECK(function_error.primary_span.has_value());
    check_span(
        *function_error.primary_span,
        function_assignment.target.name_span);
}

void user_function_calls_and_return_values_are_checked()
{
    TypeCheckResult result{R"(int identity(int value) { return value; }
bool choose(bool value) { return value; }
void notify(char value) { return; }
void inspect() {
    int number = identity(1);
    bool flag = choose(true);
    notify('x');
    (identity)(number);
})"};

    TPP_CHECK(result.check());
    const auto& identity_return = require_statement_node<ReturnStatement>(
        require_body(require_function(result.program, 0)),
        0);
    const auto& choose_return = require_statement_node<ReturnStatement>(
        require_body(require_function(result.program, 1)),
        0);
    TPP_CHECK_EQ(
        require_type(
            result.type_info,
            require_expression(identity_return.value)),
        result.types.integer_type());
    TPP_CHECK_EQ(
        require_type(
            result.type_info,
            require_expression(choose_return.value)),
        result.types.boolean_type());
    const auto& body = require_body(require_function(result.program, 3));
    const auto& number = require_statement_node<VariableDeclaration>(body, 0);
    const auto& flag = require_statement_node<VariableDeclaration>(body, 1);
    TPP_CHECK_EQ(
        require_type(result.type_info, require_expression(number.initializer)),
        result.types.integer_type());
    TPP_CHECK_EQ(
        require_type(result.type_info, require_expression(flag.initializer)),
        result.types.boolean_type());
    TPP_CHECK_EQ(
        require_type(result.type_info, require_expression_statement(body, 2)),
        result.types.void_type());
    TPP_CHECK_EQ(
        require_type(result.type_info, require_expression_statement(body, 3)),
        result.types.integer_type());
}

void user_functions_preserve_vector_signatures_and_shadow_builtins()
{
    TypeCheckResult result{R"(vector<int> keep(
    vector<int> values, string label) {
    print(label);
    return values;
}
int len(int value) { return value; }
void inspect() {
    vector<int> values = vector<int>();
    vector<int> copy = keep(values, "copy");
    int number = len(1);
})"};

    TPP_CHECK(result.check());
    const auto vector_integer = result.types.vector_type(
        result.types.integer_type());
    TPP_CHECK(vector_integer.has_value());
    const auto& keep_body = require_body(require_function(result.program, 0));
    const auto& keep_return =
        require_statement_node<ReturnStatement>(keep_body, 1);
    TPP_CHECK_EQ(
        require_type(
            result.type_info,
            require_expression(keep_return.value)),
        *vector_integer);

    const auto& inspect_body = require_body(require_function(result.program, 2));
    const auto& copy =
        require_statement_node<VariableDeclaration>(inspect_body, 1);
    const auto& number =
        require_statement_node<VariableDeclaration>(inspect_body, 2);
    TPP_CHECK_EQ(
        require_type(result.type_info, require_expression(copy.initializer)),
        *vector_integer);
    TPP_CHECK_EQ(
        require_type(result.type_info, require_expression(number.initializer)),
        result.types.integer_type());
}

void invalid_calls_and_returns_recover_independently()
{
    TypeCheckResult result{R"(int add(int left, int right) {
    return "x";
}
void notify() {
    return 1;
}
int missing_value() {
    return;
}
void inspect() {
    int variable;
    add(1);
    add(1, 2, 3);
    add("x", true);
    variable();
    int value = notify();
    add;
})"};

    TPP_CHECK(!result.check());
    TPP_CHECK_EQ(result.diagnostics.error_count(), std::size_t{10});
    const auto& inspect_body = require_body(require_function(result.program, 3));
    const auto& too_few = require_expression_statement(inspect_body, 1);
    const auto& too_many = require_expression_statement(inspect_body, 2);
    TPP_CHECK_EQ(
        require_diagnostic(result, 3).message,
        std::string{"function expects 2 arguments, got 1"});
    TPP_CHECK_EQ(
        require_diagnostic(result, 4).message,
        std::string{"function expects 2 arguments, got 3"});
    TPP_CHECK(require_diagnostic(result, 3).primary_span.has_value());
    TPP_CHECK(require_diagnostic(result, 4).primary_span.has_value());
    check_span(*require_diagnostic(result, 3).primary_span, too_few.span);
    check_span(*require_diagnostic(result, 4).primary_span, too_many.span);
    TPP_CHECK(!result.type_info.type_of(too_few).has_value());
    TPP_CHECK(!result.type_info.type_of(too_many).has_value());
}

void void_values_fail_in_value_context_without_parent_cascades()
{
    TypeCheckResult result{R"(void notify() {}
int consume(int value) { return value; }
void inspect() {
    notify() + 1;
    consume(notify());
})"};

    TPP_CHECK(!result.check());
    TPP_CHECK_EQ(result.diagnostics.error_count(), std::size_t{2});
    const auto& body = require_body(require_function(result.program, 2));
    const auto& binary = require_expression_statement(body, 0);
    const auto& outer_call = require_expression_statement(body, 1);
    TPP_CHECK(!result.type_info.type_of(binary).has_value());
    TPP_CHECK(!result.type_info.type_of(outer_call).has_value());
    const auto& binary_node = require_expression_node<BinaryExpression>(binary);
    TPP_CHECK_EQ(
        require_type(result.type_info, require_expression(binary_node.left)),
        result.types.void_type());
    const auto& call_node = require_expression_node<CallExpression>(outer_call);
    TPP_CHECK_EQ(
        require_type(
            result.type_info,
            require_expression(call_node.arguments.front())),
        result.types.void_type());
}

void conditions_require_boolean_values()
{
    TypeCheckResult valid{R"(void inspect(bool condition) {
    if condition {} else {}
    while !condition {}
})"};
    TPP_CHECK(valid.check());

    TypeCheckResult invalid{R"(void inspect() {
    if 1 {}
    while "yes" {}
})"};
    TPP_CHECK(!invalid.check());
    TPP_CHECK_EQ(invalid.diagnostics.error_count(), std::size_t{2});
    const auto& body = require_body(require_function(invalid.program, 0));
    const auto& condition =
        require_statement_node<IfStatement>(body, 0).condition;
    const auto& loop_condition =
        require_statement_node<WhileStatement>(body, 1).condition;
    TPP_CHECK(require_diagnostic(invalid, 0).primary_span.has_value());
    TPP_CHECK(require_diagnostic(invalid, 1).primary_span.has_value());
    check_span(
        *require_diagnostic(invalid, 0).primary_span,
        require_expression(condition).span);
    check_span(
        *require_diagnostic(invalid, 1).primary_span,
        require_expression(loop_condition).span);
}

void vector_construction_supports_zero_one_two_and_nested_arguments()
{
    TypeCheckResult result{R"(void inspect() {
    vector<int>();
    vector<int>(3);
    vector<int>(3, 0);
    vector<vector<int>>(2, vector<int>(1, 7));
})"};

    TPP_CHECK(result.check());
    const auto vector_integer = result.types.vector_type(
        result.types.integer_type());
    TPP_CHECK(vector_integer.has_value());
    const auto nested = result.types.vector_type(*vector_integer);
    TPP_CHECK(nested.has_value());
    const auto& body = require_body(require_function(result.program, 0));
    for (std::size_t index = 0; index < 3; ++index) {
        TPP_CHECK_EQ(
            require_type(
                result.type_info,
                require_expression_statement(body, index)),
            *vector_integer);
    }
    const auto& nested_expression = require_expression_statement(body, 3);
    TPP_CHECK_EQ(require_type(result.type_info, nested_expression), *nested);
    const auto& construction =
        require_expression_node<VectorConstructionExpression>(nested_expression);
    TPP_CHECK_EQ(
        require_type(
            result.type_info,
            require_expression(construction.arguments[1])),
        *vector_integer);
}

void invalid_vector_arguments_do_not_produce_a_fake_type()
{
    TypeCheckResult result{R"(void inspect() {
    vector<int>(true);
    vector<int>(1, "x");
    vector<int>(1, 0, 0);
    vector<vector<int>>(1, vector<string>());
    vector<int>(true, "bad");
})"};

    TPP_CHECK(!result.check());
    TPP_CHECK_EQ(result.diagnostics.error_count(), std::size_t{6});
    const auto& body = require_body(require_function(result.program, 0));
    for (std::size_t index = 0; index < body.items.size(); ++index) {
        TPP_CHECK(!result.type_info.type_of(
            require_expression_statement(body, index)).has_value());
    }
}

void vector_and_string_indexing_record_result_types()
{
    TypeCheckResult result{R"(void inspect() {
    vector<vector<int>> matrix = vector<vector<int>>();
    string text = "abc";
    matrix[0];
    matrix[0][1];
    text[2];
})"};

    TPP_CHECK(result.check());
    const auto vector_integer = result.types.vector_type(
        result.types.integer_type());
    TPP_CHECK(vector_integer.has_value());
    const auto& body = require_body(require_function(result.program, 0));
    TPP_CHECK_EQ(
        require_type(result.type_info, require_expression_statement(body, 2)),
        *vector_integer);
    TPP_CHECK_EQ(
        require_type(result.type_info, require_expression_statement(body, 3)),
        result.types.integer_type());
    TPP_CHECK_EQ(
        require_type(result.type_info, require_expression_statement(body, 4)),
        result.types.character_type());
}

void invalid_indexing_reports_the_index_or_base_span()
{
    TypeCheckResult result{R"(void inspect() {
    vector<int> values;
    values[true];
    1[0];
    (true + false)[true];
})"};

    TPP_CHECK(!result.check());
    TPP_CHECK_EQ(result.diagnostics.error_count(), std::size_t{4});
    const auto& body = require_body(require_function(result.program, 0));
    const auto& invalid_index = require_expression_node<IndexExpression>(
        require_expression_statement(body, 1));
    const auto& invalid_base = require_expression_node<IndexExpression>(
        require_expression_statement(body, 2));
    TPP_CHECK(require_diagnostic(result, 0).primary_span.has_value());
    TPP_CHECK(require_diagnostic(result, 1).primary_span.has_value());
    check_span(
        *require_diagnostic(result, 0).primary_span,
        require_expression(invalid_index.index).span);
    check_span(
        *require_diagnostic(result, 1).primary_span,
        require_expression(invalid_base.base).span);
}

void assignment_target_indices_are_checked_at_every_level()
{
    TypeCheckResult valid{R"(void inspect() {
    vector<vector<int>> matrix;
    matrix[0][1] = 2;
})"};
    TPP_CHECK(valid.check());
    const auto& valid_body = require_body(require_function(valid.program, 0));
    const auto& assignment =
        require_statement_node<AssignmentStatement>(valid_body, 1);
    TPP_CHECK_EQ(
        require_type(valid.type_info, assignment.target),
        valid.types.integer_type());

    TypeCheckResult invalid{R"(void inspect() {
    vector<vector<int>> matrix;
    matrix[true][0] = 1;
    matrix[0][false] = 1;
})"};
    TPP_CHECK(!invalid.check());
    TPP_CHECK_EQ(invalid.diagnostics.error_count(), std::size_t{2});
    const auto& invalid_body = require_body(require_function(invalid.program, 0));
    for (std::size_t index = 0; index < 2; ++index) {
        const auto& invalid_assignment =
            require_statement_node<AssignmentStatement>(invalid_body, index + 1);
        TPP_CHECK(!invalid.type_info.type_of(
            invalid_assignment.target).has_value());
        TPP_CHECK(require_diagnostic(invalid, index).primary_span.has_value());
        const auto expected_index = index == 0 ? std::size_t{0} : std::size_t{1};
        check_span(
            *require_diagnostic(invalid, index).primary_span,
            require_expression(
                invalid_assignment.target.indices[expected_index]).span);
    }
}

void range_and_foreach_loops_are_checked_and_inferred()
{
    TypeCheckResult result{R"(void inspect() {
    vector<int> values = vector<int>();
    string text = "abc";
    vector<vector<int>> matrix = vector<vector<int>>();
    for index in 0..3 { index + 1; }
    for value in values { value + 1; }
    for character in text { character == 'a'; }
    for row in matrix { row[0]; }
})"};

    TPP_CHECK(result.check());
    const auto& body = require_body(require_function(result.program, 0));
    const auto& range = require_statement_node<ForRangeStatement>(body, 3);
    const auto& values = require_statement_node<ForEachStatement>(body, 4);
    const auto& text = require_statement_node<ForEachStatement>(body, 5);
    const auto& matrix = require_statement_node<ForEachStatement>(body, 6);
    const auto values_symbol = require_symbol(result.declarations.symbol_for(values));
    const auto text_symbol = require_symbol(result.declarations.symbol_for(text));
    const auto matrix_symbol = require_symbol(result.declarations.symbol_for(matrix));
    const auto vector_integer = result.types.vector_type(
        result.types.integer_type());
    TPP_CHECK(vector_integer.has_value());

    TPP_CHECK_EQ(
        result.type_info.inferred_type(values_symbol),
        std::optional<TypeId>{result.types.integer_type()});
    TPP_CHECK_EQ(
        result.type_info.inferred_type(text_symbol),
        std::optional<TypeId>{result.types.character_type()});
    TPP_CHECK_EQ(
        result.type_info.inferred_type(matrix_symbol),
        std::optional<TypeId>{*vector_integer});
    TPP_CHECK(range.body != nullptr);
    TPP_CHECK(values.body != nullptr);
    TPP_CHECK(text.body != nullptr);
    TPP_CHECK(matrix.body != nullptr);
    TPP_CHECK_EQ(
        require_type(
            result.type_info,
            require_expression_statement(*range.body, 0)),
        result.types.integer_type());
    TPP_CHECK_EQ(
        require_type(
            result.type_info,
            require_expression_statement(*values.body, 0)),
        result.types.integer_type());
    TPP_CHECK_EQ(
        require_type(
            result.type_info,
            require_expression_statement(*text.body, 0)),
        result.types.boolean_type());
    TPP_CHECK_EQ(
        require_type(
            result.type_info,
            require_expression_statement(*matrix.body, 0)),
        result.types.integer_type());
}

void nested_foreach_bindings_can_be_used_and_assigned()
{
    TypeCheckResult result{R"(void inspect() {
    vector<vector<int>> rows = vector<vector<int>>();
    string text = "abc";
    for row in rows {
        for value in row { value = 1; }
    }
    for character in text { character = 'x'; }
})"};

    TPP_CHECK(result.check());
    const auto& body = require_body(require_function(result.program, 0));
    const auto& outer = require_statement_node<ForEachStatement>(body, 2);
    TPP_CHECK(outer.body != nullptr);
    const auto& inner =
        require_statement_node<ForEachStatement>(*outer.body, 0);
    const auto& characters =
        require_statement_node<ForEachStatement>(body, 3);
    const auto outer_symbol =
        require_symbol(result.declarations.symbol_for(outer));
    const auto inner_symbol =
        require_symbol(result.declarations.symbol_for(inner));
    const auto character_symbol =
        require_symbol(result.declarations.symbol_for(characters));
    const auto vector_integer =
        result.types.vector_type(result.types.integer_type());
    TPP_CHECK(vector_integer.has_value());

    TPP_CHECK_EQ(
        result.type_info.inferred_type(outer_symbol),
        std::optional<TypeId>{*vector_integer});
    TPP_CHECK_EQ(
        result.type_info.inferred_type(inner_symbol),
        std::optional<TypeId>{result.types.integer_type()});
    TPP_CHECK_EQ(
        result.type_info.inferred_type(character_symbol),
        std::optional<TypeId>{result.types.character_type()});

    TPP_CHECK(inner.body != nullptr);
    const auto& integer_assignment =
        require_statement_node<AssignmentStatement>(*inner.body, 0);
    TPP_CHECK_EQ(
        require_type(result.type_info, integer_assignment.target),
        result.types.integer_type());
    TPP_CHECK(characters.body != nullptr);
    const auto& character_assignment =
        require_statement_node<AssignmentStatement>(*characters.body, 0);
    TPP_CHECK_EQ(
        require_type(result.type_info, character_assignment.target),
        result.types.character_type());
}

void invalid_loops_recover_without_cascading_from_foreach_binding()
{
    TypeCheckResult result{R"(void inspect() {
    for index in true..1 {}
    for index in 0.."end" {}
    for value in 1 { value + 1; }
})"};

    TPP_CHECK(!result.check());
    TPP_CHECK_EQ(result.diagnostics.error_count(), std::size_t{3});
    const auto& body = require_body(require_function(result.program, 0));
    const auto& each = require_statement_node<ForEachStatement>(body, 2);
    const auto each_symbol = require_symbol(result.declarations.symbol_for(each));
    TPP_CHECK(!result.type_info.inferred_type(each_symbol).has_value());
    TPP_CHECK(each.body != nullptr);
    const auto& body_expression = require_expression_statement(*each.body, 0);
    TPP_CHECK(!result.type_info.type_of(body_expression).has_value());
}

void all_builtin_signatures_produce_canonical_types()
{
    TypeCheckResult result{R"(void inspect() {
    print(1); print(true); print('x'); print("x");
    read_int(); read_string(); read_char();
    len("text");
    substring("text", 0, 2);
})"};

    TPP_CHECK(result.check());
    const auto& body = require_body(require_function(result.program, 0));
    for (std::size_t index = 0; index < 4; ++index) {
        TPP_CHECK_EQ(
            require_type(
                result.type_info,
                require_expression_statement(body, index)),
            result.types.void_type());
    }
    TPP_CHECK_EQ(
        require_type(result.type_info, require_expression_statement(body, 4)),
        result.types.integer_type());
    TPP_CHECK_EQ(
        require_type(result.type_info, require_expression_statement(body, 5)),
        result.types.string_type());
    TPP_CHECK_EQ(
        require_type(result.type_info, require_expression_statement(body, 6)),
        result.types.character_type());
    TPP_CHECK_EQ(
        require_type(result.type_info, require_expression_statement(body, 7)),
        result.types.integer_type());
    TPP_CHECK_EQ(
        require_type(result.type_info, require_expression_statement(body, 8)),
        result.types.string_type());
}

void invalid_builtin_calls_check_arity_and_argument_types()
{
    TypeCheckResult result{R"(void inspect() {
    print();
    print(vector<int>());
    read_int(1);
    read_string(1);
    read_char(1);
    len(1);
    len("a", "b");
    substring("text", true, 1);
    substring("text", 0);
})"};

    TPP_CHECK(!result.check());
    TPP_CHECK_EQ(result.diagnostics.error_count(), std::size_t{9});
    const auto& body = require_body(require_function(result.program, 0));
    for (std::size_t index = 0; index < body.items.size(); ++index) {
        TPP_CHECK(!result.type_info.type_of(
            require_expression_statement(body, index)).has_value());
    }
}

void string_members_are_typed_and_resolved_by_member_kind()
{
    TypeCheckResult result{R"(void inspect() {
    string text = "abc";
    vector<string> values = vector<string>(1, "item");
    text.length();
    text.push('x');
    (text).push('y');
    values[0].push('z');
})"};

    TPP_CHECK(result.check());
    const auto& body = require_body(require_function(result.program, 0));
    const auto& length_call = require_expression_node<CallExpression>(
        require_expression_statement(body, 2));
    const auto& length = require_expression_node<MemberAccessExpression>(
        require_expression(length_call.callee));
    const auto& push_call = require_expression_node<CallExpression>(
        require_expression_statement(body, 3));
    const auto& push = require_expression_node<MemberAccessExpression>(
        require_expression(push_call.callee));

    TPP_CHECK_EQ(
        result.type_info.member_for(length),
        std::optional<MemberKind>{MemberKind::string_length});
    TPP_CHECK_EQ(
        result.type_info.member_for(push),
        std::optional<MemberKind>{MemberKind::string_push});
    TPP_CHECK_EQ(
        require_type(
            result.type_info,
            require_expression_statement(body, 2)),
        result.types.integer_type());
    for (std::size_t index = 3; index < body.items.size(); ++index) {
        const auto& call = require_expression_node<CallExpression>(
            require_expression_statement(body, index));
        const auto& member = require_expression_node<MemberAccessExpression>(
            require_expression(call.callee));
        TPP_CHECK_EQ(
            result.type_info.member_for(member),
            std::optional<MemberKind>{MemberKind::string_push});
        TPP_CHECK_EQ(
            require_type(
                result.type_info,
                require_expression_statement(body, index)),
            result.types.void_type());
    }
}

void mutable_string_receivers_cover_all_storage_kinds()
{
    TypeCheckResult result{R"(string global_text = "global";
void inspect(string parameter, vector<string> values) {
    global_text.push('g');
    parameter.push('p');
    for value in values { value.push('v'); }
})"};

    TPP_CHECK(result.check());
    const auto& body = require_body(require_function(result.program, 1));
    TPP_CHECK_EQ(
        require_type(result.type_info, require_expression_statement(body, 0)),
        result.types.void_type());
    TPP_CHECK_EQ(
        require_type(result.type_info, require_expression_statement(body, 1)),
        result.types.void_type());

    const auto& loop = require_statement_node<ForEachStatement>(body, 2);
    const auto loop_symbol =
        require_symbol(result.declarations.symbol_for(loop));
    TPP_CHECK_EQ(
        result.type_info.inferred_type(loop_symbol),
        std::optional<TypeId>{result.types.string_type()});
    TPP_CHECK(loop.body != nullptr);
    TPP_CHECK_EQ(
        require_type(
            result.type_info,
            require_expression_statement(*loop.body, 0)),
        result.types.void_type());
}

void string_push_rejects_temporary_receivers_at_the_receiver_span()
{
    TypeCheckResult result{R"(string make_string() { return "made"; }
void inspect() {
    "literal".push('a');
    ("left" + "right").push('b');
    read_string().push('c');
    make_string().push('d');
})"};

    TPP_CHECK(!result.check());
    TPP_CHECK_EQ(result.diagnostics.error_count(), std::size_t{4});
    const auto& body = require_body(require_function(result.program, 1));
    for (std::size_t index = 0; index < body.items.size(); ++index) {
        const auto& expression = require_expression_statement(body, index);
        const auto& call = require_expression_node<CallExpression>(expression);
        const auto& member = require_expression_node<MemberAccessExpression>(
            require_expression(call.callee));
        const auto& receiver = require_expression(member.base);
        const auto& diagnostic = require_diagnostic(result, index);

        TPP_CHECK_EQ(diagnostic.severity, DiagnosticSeverity::error);
        TPP_CHECK_EQ(
            diagnostic.message,
            std::string{
                "member function 'push' requires a mutable string receiver"});
        TPP_CHECK(diagnostic.primary_span.has_value());
        check_span(*diagnostic.primary_span, receiver.span);
        TPP_CHECK(!result.type_info.type_of(expression).has_value());
        TPP_CHECK_EQ(
            result.type_info.member_for(member),
            std::optional<MemberKind>{MemberKind::string_push});
    }
}

void invalid_member_and_non_callable_usage_is_diagnosed_once()
{
    TypeCheckResult result{R"(void inspect() {
    string text = "abc";
    text.missing();
    1.length();
    text.push(1);
    text.length(1);
    text.length;
})"};

    TPP_CHECK(!result.check());
    TPP_CHECK_EQ(result.diagnostics.error_count(), std::size_t{5});
    const auto& body = require_body(require_function(result.program, 0));
    for (std::size_t index = 1; index < body.items.size(); ++index) {
        TPP_CHECK(!result.type_info.type_of(
            require_expression_statement(body, index)).has_value());
    }
    const auto& missing_call = require_expression_node<CallExpression>(
        require_expression_statement(body, 1));
    const auto& missing = require_expression_node<MemberAccessExpression>(
        require_expression(missing_call.callee));
    TPP_CHECK(!result.type_info.member_for(missing).has_value());
    const auto& first = require_diagnostic(result, 0);
    TPP_CHECK(first.primary_span.has_value());
    check_span(*first.primary_span, require_expression(missing_call.callee).span);
}

void integer_literals_enforce_signed_64_bit_boundaries()
{
    TypeCheckResult valid{R"(void inspect() {
    00042;
    9223372036854775807;
    -9223372036854775808;
    -(9223372036854775808);
})"};
    TPP_CHECK(valid.check());
    const auto& valid_body = require_body(require_function(valid.program, 0));
    for (std::size_t index = 0; index < valid_body.items.size(); ++index) {
        TPP_CHECK_EQ(
            require_type(
                valid.type_info,
                require_expression_statement(valid_body, index)),
            valid.types.integer_type());
    }

    TypeCheckResult invalid{R"(void inspect() {
    9223372036854775808;
    -9223372036854775809;
    --9223372036854775808;
    -(-9223372036854775808);
    -(+(-9223372036854775808));
})"};
    TPP_CHECK(!invalid.check());
    TPP_CHECK_EQ(invalid.diagnostics.error_count(), std::size_t{5});
    const auto& invalid_body = require_body(require_function(invalid.program, 0));
    for (std::size_t index = 0; index < invalid_body.items.size(); ++index) {
        const auto& diagnostic = require_diagnostic(invalid, index);
        TPP_CHECK_EQ(
            diagnostic.message,
            index < 2
                ? std::string{
                      "integer literal is outside the signed 64-bit range"}
                : std::string{
                      "integer expression is outside the signed 64-bit range"});
        TPP_CHECK(diagnostic.primary_span.has_value());
        const auto& expression = require_expression_statement(
            invalid_body,
            index);
        const Expression* diagnostic_expression = &expression;
        if (index < 2) {
            if (const auto* unary =
                    std::get_if<UnaryExpression>(&expression.node)) {
                diagnostic_expression =
                    &require_expression(unary->operand);
            }
        }
        check_span(*diagnostic.primary_span, diagnostic_expression->span);
    }
}

void global_initializer_forms_are_restricted_without_inner_cascades()
{
    TypeCheckResult valid{R"(int number = -(1 + 2 * 3);
bool flag = (true && false) || !false;
string text = "constant" + " expression";
bool ordered = "a" < "b";
vector<int> empty;
)"};
    TPP_CHECK(valid.check());

    TypeCheckResult invalid{R"(int source;
int by_name = source;
int by_call = read_int();
char by_index = "x"[0];
int by_member = "x".length();
vector<int> by_vector = vector<int>();
)"};
    TPP_CHECK(!invalid.check());
    TPP_CHECK_EQ(invalid.diagnostics.error_count(), std::size_t{5});
    for (std::size_t index = 0; index < 5; ++index) {
        const auto& declaration = require_global_variable(
            invalid.program,
            index + 1);
        const auto& diagnostic = require_diagnostic(invalid, index);
        TPP_CHECK_EQ(
            diagnostic.message,
            std::string{
                "global variable initializer must be a constant expression"});
        TPP_CHECK(diagnostic.primary_span.has_value());
        TPP_CHECK(declaration.initializer != nullptr);
        check_span(*diagnostic.primary_span, declaration.initializer->span);
        TPP_CHECK(!invalid.type_info.type_of(*declaration.initializer).has_value());
    }
}

void independent_errors_continue_while_failed_subtrees_do_not_cascade()
{
    TypeCheckResult result{R"(void inspect() {
    int first = "x";
    bool second = 1;
    1 + (true * "x");
    if vector<int>() {}
})"};

    TPP_CHECK(!result.check());
    TPP_CHECK_EQ(result.diagnostics.error_count(), std::size_t{4});
    const auto& body = require_body(require_function(result.program, 0));
    const auto& outer = require_expression_statement(body, 2);
    const auto& outer_node = require_expression_node<BinaryExpression>(outer);
    const auto& parentheses = require_expression_node<ParenthesizedExpression>(
        require_expression(outer_node.right));
    TPP_CHECK(!result.type_info.type_of(
        require_expression(parentheses.expression)).has_value());
    TPP_CHECK(!result.type_info.type_of(outer).has_value());
}

void nested_function_return_context_and_non_indexable_targets_are_checked()
{
    TypeCheckResult valid{R"(int outer() {
    void nested() { return; }
    return 1;
})"};
    TPP_CHECK(valid.check());

    TypeCheckResult invalid{R"(int outer() {
    void nested() { return 1; }
    int value;
    value[0] = 1;
    return;
})"};
    TPP_CHECK(!invalid.check());
    TPP_CHECK_EQ(invalid.diagnostics.error_count(), std::size_t{3});
    auto& outer = require_function(invalid.program, 0);
    auto& body = require_body(outer);
    auto& nested = require_variant<FunctionDeclaration>(body.items[0]);
    const auto& nested_return =
        require_statement_node<ReturnStatement>(require_body(nested), 0);
    const auto& assignment =
        require_statement_node<AssignmentStatement>(body, 2);
    const auto& outer_return =
        require_statement_node<ReturnStatement>(body, 3);

    TPP_CHECK(require_diagnostic(invalid, 0).primary_span.has_value());
    check_span(
        *require_diagnostic(invalid, 0).primary_span,
        require_expression(nested_return.value).span);
    TPP_CHECK(require_diagnostic(invalid, 1).primary_span.has_value());
    check_span(
        *require_diagnostic(invalid, 1).primary_span,
        assignment.target.span);
    TPP_CHECK(require_diagnostic(invalid, 2).primary_span.has_value());
    check_span(
        *require_diagnostic(invalid, 2).primary_span,
        require_statement(body, 3).span);
    TPP_CHECK(outer_return.value == nullptr);
}

void fresh_type_info_has_no_foreign_mappings()
{
    TypeInfo info;
    const SourceSpan span{SourceId{0}, 0, 1};
    const Expression expression{
        .span = span,
        .node = tpp::IntegerLiteralExpression{.lexeme = "1"},
    };
    const AssignmentTarget target{
        .span = span,
        .name = "x",
        .name_span = span,
        .indices = {},
    };
    const MemberAccessExpression member{
        .base = nullptr,
        .member = "length",
    };

    TPP_CHECK(info.empty());
    TPP_CHECK(!info.type_of(expression).has_value());
    TPP_CHECK(!info.type_of(target).has_value());
    TPP_CHECK(!info.inferred_type(SymbolId{42}).has_value());
    TPP_CHECK(!info.member_for(member).has_value());
}

void malformed_recursive_ast_is_diagnosed_without_crashing()
{
    TypeCheckResult result{R"(void inspect() {
    -1;
    1 + 2;
    (1);
    vector<int>(1);
    "x"[0];
    print(1);
    -1;
    int value;
    value += 1;
})"};
    auto& body = require_body(require_function(result.program, 0));
    require_expression_node<UnaryExpression>(
        require_expression_statement(body, 0)).operand.reset();
    require_expression_node<BinaryExpression>(
        require_expression_statement(body, 1)).right.reset();
    require_expression_node<ParenthesizedExpression>(
        require_expression_statement(body, 2)).expression.reset();
    require_expression_node<VectorConstructionExpression>(
        require_expression_statement(body, 3)).arguments[0].reset();
    require_expression_node<IndexExpression>(
        require_expression_statement(body, 4)).index.reset();
    require_expression_node<CallExpression>(
        require_expression_statement(body, 5)).callee.reset();
    require_expression_node<UnaryExpression>(
        require_expression_statement(body, 6)).operator_kind =
        static_cast<tpp::UnaryOperator>(999);
    require_statement_node<AssignmentStatement>(body, 8).operator_kind =
        static_cast<tpp::AssignmentOperator>(999);

    TPP_CHECK(!result.check());
    TPP_CHECK_EQ(result.diagnostics.error_count(), std::size_t{8});
    for (const auto& diagnostic : result.diagnostics.diagnostics()) {
        TPP_CHECK_EQ(diagnostic.severity, DiagnosticSeverity::error);
        TPP_CHECK(diagnostic.primary_span.has_value());
        tpp::test::check_contains(diagnostic.message, "malformed AST");
    }
}

}

int main()
{
    return tpp::test::run({
        {"literal and nested expression types",
         literal_and_nested_expression_types_are_recorded},
        {"valid unary and binary operators",
         every_valid_unary_and_binary_operator_is_checked},
        {"string addition and ordering",
         string_addition_and_ordering_have_stable_types},
        {"invalid operators and cascade suppression",
         invalid_operators_report_once_per_expression_without_cascades},
        {"initializers and assignments",
         initializers_assignments_and_compound_assignments_are_checked},
        {"invalid initializers and assignments",
         invalid_initializers_and_assignments_have_precise_spans},
        {"function calls and returns",
         user_function_calls_and_return_values_are_checked},
        {"function vector signatures and builtin shadowing",
         user_functions_preserve_vector_signatures_and_shadow_builtins},
        {"invalid calls and returns",
         invalid_calls_and_returns_recover_independently},
        {"void values and cascade suppression",
         void_values_fail_in_value_context_without_parent_cascades},
        {"boolean conditions", conditions_require_boolean_values},
        {"valid vector construction",
         vector_construction_supports_zero_one_two_and_nested_arguments},
        {"invalid vector construction",
         invalid_vector_arguments_do_not_produce_a_fake_type},
        {"vector and string indexing",
         vector_and_string_indexing_record_result_types},
        {"invalid indexing spans",
         invalid_indexing_reports_the_index_or_base_span},
        {"assignment target index types",
         assignment_target_indices_are_checked_at_every_level},
        {"range and foreach inference",
         range_and_foreach_loops_are_checked_and_inferred},
        {"nested foreach inference and assignment",
         nested_foreach_bindings_can_be_used_and_assigned},
        {"invalid loops and cascade suppression",
         invalid_loops_recover_without_cascading_from_foreach_binding},
        {"builtin signatures",
         all_builtin_signatures_produce_canonical_types},
        {"invalid builtin calls",
         invalid_builtin_calls_check_arity_and_argument_types},
        {"string member signatures",
         string_members_are_typed_and_resolved_by_member_kind},
        {"mutable string receiver kinds",
         mutable_string_receivers_cover_all_storage_kinds},
        {"string push receiver categories",
         string_push_rejects_temporary_receivers_at_the_receiver_span},
        {"invalid member usage",
         invalid_member_and_non_callable_usage_is_diagnosed_once},
        {"integer boundaries",
         integer_literals_enforce_signed_64_bit_boundaries},
        {"global initializer forms",
         global_initializer_forms_are_restricted_without_inner_cascades},
        {"independent errors",
         independent_errors_continue_while_failed_subtrees_do_not_cascade},
        {"nested return context and assignment target",
         nested_function_return_context_and_non_indexable_targets_are_checked},
        {"fresh type info", fresh_type_info_has_no_foreign_mappings},
        {"malformed AST safety",
         malformed_recursive_ast_is_diagnosed_without_crashing},
    });
}

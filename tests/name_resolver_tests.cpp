#include "test_support.hpp"

#include "pseudo/ast/program.hpp"
#include "pseudo/diagnostics/diagnostic.hpp"
#include "pseudo/diagnostics/diagnostic_engine.hpp"
#include "pseudo/lexer/lexer.hpp"
#include "pseudo/parser/parser.hpp"
#include "pseudo/semantic/builtin.hpp"
#include "pseudo/semantic/declaration_collector.hpp"
#include "pseudo/semantic/declaration_info.hpp"
#include "pseudo/semantic/name_resolver.hpp"
#include "pseudo/semantic/resolution_info.hpp"
#include "pseudo/semantic/symbol_table.hpp"
#include "pseudo/semantic/type_context.hpp"
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
using tpp::BlockStatement;
using tpp::BuiltinFunctionKind;
using tpp::CallExpression;
using tpp::DeclarationCollector;
using tpp::DeclarationInfo;
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
using tpp::NameResolver;
using tpp::ParenthesizedExpression;
using tpp::Parser;
using tpp::Program;
using tpp::ResolutionInfo;
using tpp::ResolutionTarget;
using tpp::ReturnStatement;
using tpp::SourceId;
using tpp::SourceManager;
using tpp::SourceSpan;
using tpp::Statement;
using tpp::SymbolId;
using tpp::SymbolTable;
using tpp::Token;
using tpp::TypeContext;
using tpp::UnaryExpression;
using tpp::VariableDeclaration;
using tpp::VectorConstructionExpression;
using tpp::WhileStatement;

class ResolutionResult {
public:
    explicit ResolutionResult(std::string contents)
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
        succeeded = resolver.resolve(program);
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
    bool succeeded = false;
};

template <typename Node, typename Variant>
const Node& require_variant(const Variant& variant)
{
    const auto* node = std::get_if<Node>(&variant);
    TPP_CHECK(node != nullptr);
    return *node;
}

template <typename Node>
const Node& require_expression_node(const Expression& expression)
{
    return require_variant<Node>(expression.node);
}

const FunctionDeclaration& require_function(
    const Program& program,
    const std::size_t index)
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

const Statement& require_statement(const Block& block, const std::size_t index)
{
    TPP_CHECK(index < block.items.size());
    return require_variant<Statement>(block.items[index]);
}

template <typename Node>
const Node& require_statement_node(const Block& block, const std::size_t index)
{
    return require_variant<Node>(require_statement(block, index).node);
}

const FunctionDeclaration& require_nested_function(
    const Block& block,
    const std::size_t index)
{
    TPP_CHECK(index < block.items.size());
    return require_variant<FunctionDeclaration>(block.items[index]);
}

const Block& require_body(const FunctionDeclaration& function)
{
    TPP_CHECK(function.body != nullptr);
    return *function.body;
}

const Expression& require_expression(const tpp::ExpressionPtr& expression)
{
    TPP_CHECK(expression != nullptr);
    return *expression;
}

SymbolId require_declaration_symbol(const std::optional<SymbolId> symbol)
{
    TPP_CHECK(symbol.has_value());
    return *symbol;
}

ResolutionTarget require_resolution(
    const ResolutionInfo& resolutions,
    const IdentifierExpression& identifier)
{
    const auto target = resolutions.resolution_for(identifier);
    TPP_CHECK(target.has_value());
    return *target;
}

ResolutionTarget require_resolution(
    const ResolutionInfo& resolutions,
    const AssignmentTarget& target)
{
    const auto resolution = resolutions.resolution_for(target);
    TPP_CHECK(resolution.has_value());
    return *resolution;
}

SymbolId require_symbol_resolution(
    const ResolutionInfo& resolutions,
    const IdentifierExpression& identifier)
{
    const auto target = require_resolution(resolutions, identifier);
    const auto* symbol = std::get_if<SymbolId>(&target);
    TPP_CHECK(symbol != nullptr);
    return *symbol;
}

SymbolId require_symbol_resolution(
    const ResolutionInfo& resolutions,
    const AssignmentTarget& assignment)
{
    const auto target = require_resolution(resolutions, assignment);
    const auto* symbol = std::get_if<SymbolId>(&target);
    TPP_CHECK(symbol != nullptr);
    return *symbol;
}

BuiltinFunctionKind require_builtin_resolution(
    const ResolutionInfo& resolutions,
    const IdentifierExpression& identifier)
{
    const auto target = require_resolution(resolutions, identifier);
    const auto* builtin = std::get_if<BuiltinFunctionKind>(&target);
    TPP_CHECK(builtin != nullptr);
    return *builtin;
}

const IdentifierExpression& require_identifier(const Expression& expression)
{
    return require_expression_node<IdentifierExpression>(expression);
}

const IdentifierExpression& require_expression_statement_identifier(
    const Block& block,
    const std::size_t index)
{
    const auto& statement =
        require_statement_node<ExpressionStatement>(block, index);
    return require_identifier(require_expression(statement.expression));
}

void check_span(const SourceSpan actual, const SourceSpan expected)
{
    TPP_CHECK_EQ(actual.source, expected.source);
    TPP_CHECK_EQ(actual.begin, expected.begin);
    TPP_CHECK_EQ(actual.end, expected.end);
}

void globals_parameters_locals_and_initializers_resolve()
{
    ResolutionResult result{
        "int first = later;"
        "int later;"
        "int helper(int parameter) {"
        "int local = parameter;"
        "local;"
        "first;"
        "return later;"
        "}"};

    TPP_CHECK(result.succeeded);
    TPP_CHECK(!result.diagnostics.has_errors());

    const auto& first = require_global_variable(result.program, 0);
    const auto& later = require_global_variable(result.program, 1);
    const auto& helper = require_function(result.program, 2);
    const auto& body = require_body(helper);
    const auto& parameter = helper.parameters.front();
    const auto& local = require_statement_node<VariableDeclaration>(body, 0);

    const auto first_symbol =
        require_declaration_symbol(result.declarations.symbol_for(first));
    const auto later_symbol =
        require_declaration_symbol(result.declarations.symbol_for(later));
    const auto parameter_symbol =
        require_declaration_symbol(result.declarations.symbol_for(parameter));
    const auto local_symbol =
        require_declaration_symbol(result.declarations.symbol_for(local));

    const auto& global_initializer =
        require_identifier(require_expression(first.initializer));
    const auto& local_initializer =
        require_identifier(require_expression(local.initializer));
    const auto& local_use = require_expression_statement_identifier(body, 1);
    const auto& global_use = require_expression_statement_identifier(body, 2);
    const auto& return_statement =
        require_statement_node<ReturnStatement>(body, 3);
    const auto& return_use =
        require_identifier(require_expression(return_statement.value));

    TPP_CHECK_EQ(
        require_symbol_resolution(result.resolutions, global_initializer),
        later_symbol);
    TPP_CHECK_EQ(
        require_symbol_resolution(result.resolutions, local_initializer),
        parameter_symbol);
    TPP_CHECK_EQ(
        require_symbol_resolution(result.resolutions, local_use),
        local_symbol);
    TPP_CHECK_EQ(
        require_symbol_resolution(result.resolutions, global_use),
        first_symbol);
    TPP_CHECK_EQ(
        require_symbol_resolution(result.resolutions, return_use),
        later_symbol);
}

void locals_are_ordered_with_outer_and_builtin_fallbacks()
{
    ResolutionResult result{R"(int value;
int main() {
    value;
    int value = value;
    value;
    print(0);
    int print;
    print;
    missing;
    int missing;
    int self = self;
    future = 0;
    int future;
}
)"};

    TPP_CHECK(!result.succeeded);
    TPP_CHECK_EQ(result.diagnostics.error_count(), std::size_t{3});
    TPP_CHECK_EQ(result.diagnostics.diagnostics().size(), std::size_t{6});

    const auto& global = require_global_variable(result.program, 0);
    const auto global_symbol =
        require_declaration_symbol(result.declarations.symbol_for(global));
    const auto& main_function = require_function(result.program, 1);
    const auto& body = require_body(main_function);
    const auto& value = require_statement_node<VariableDeclaration>(body, 1);
    const auto& print = require_statement_node<VariableDeclaration>(body, 4);
    const auto& missing = require_statement_node<VariableDeclaration>(body, 7);
    const auto& self = require_statement_node<VariableDeclaration>(body, 8);
    const auto& future_assignment =
        require_statement_node<AssignmentStatement>(body, 9);
    const auto& future =
        require_statement_node<VariableDeclaration>(body, 10);
    const auto value_symbol =
        require_declaration_symbol(result.declarations.symbol_for(value));
    const auto print_symbol =
        require_declaration_symbol(result.declarations.symbol_for(print));

    const auto& before_value = require_expression_statement_identifier(body, 0);
    const auto& value_initializer =
        require_identifier(require_expression(value.initializer));
    const auto& after_value = require_expression_statement_identifier(body, 2);
    const auto& print_call_statement =
        require_statement_node<ExpressionStatement>(body, 3);
    const auto& print_call = require_expression_node<CallExpression>(
        require_expression(print_call_statement.expression));
    const auto& builtin_print =
        require_identifier(require_expression(print_call.callee));
    const auto& after_print = require_expression_statement_identifier(body, 5);
    const auto& missing_use = require_expression_statement_identifier(body, 6);
    const auto& self_use = require_identifier(require_expression(self.initializer));

    TPP_CHECK_EQ(
        require_symbol_resolution(result.resolutions, before_value),
        global_symbol);
    TPP_CHECK_EQ(
        require_symbol_resolution(result.resolutions, value_initializer),
        global_symbol);
    TPP_CHECK_EQ(
        require_symbol_resolution(result.resolutions, after_value),
        value_symbol);
    TPP_CHECK_EQ(
        require_builtin_resolution(result.resolutions, builtin_print),
        BuiltinFunctionKind::print);
    TPP_CHECK_EQ(
        require_symbol_resolution(result.resolutions, after_print),
        print_symbol);
    TPP_CHECK(!result.resolutions.resolution_for(missing_use).has_value());
    TPP_CHECK(!result.resolutions.resolution_for(self_use).has_value());
    TPP_CHECK(
        !result.resolutions.resolution_for(future_assignment.target).has_value());

    const auto diagnostics = result.diagnostics.diagnostics();
    TPP_CHECK_EQ(
        diagnostics[0].message,
        std::string{"name 'missing' is used before its declaration"});
    TPP_CHECK_EQ(diagnostics[0].severity, DiagnosticSeverity::error);
    TPP_CHECK(diagnostics[0].primary_span.has_value());
    check_span(*diagnostics[0].primary_span, require_expression(
        require_statement_node<ExpressionStatement>(body, 6).expression).span);
    TPP_CHECK_EQ(diagnostics[1].message, std::string{"declaration is here"});
    TPP_CHECK_EQ(diagnostics[1].severity, DiagnosticSeverity::note);
    TPP_CHECK(diagnostics[1].primary_span.has_value());
    check_span(*diagnostics[1].primary_span, missing.name_span);
    TPP_CHECK_EQ(
        diagnostics[2].message,
        std::string{"name 'self' is used before its declaration"});
    TPP_CHECK_EQ(diagnostics[2].severity, DiagnosticSeverity::error);
    TPP_CHECK(diagnostics[2].primary_span.has_value());
    check_span(*diagnostics[2].primary_span, self.initializer->span);
    TPP_CHECK_EQ(diagnostics[3].message, std::string{"declaration is here"});
    TPP_CHECK_EQ(diagnostics[3].severity, DiagnosticSeverity::note);
    TPP_CHECK(diagnostics[3].primary_span.has_value());
    check_span(*diagnostics[3].primary_span, self.name_span);
    TPP_CHECK_EQ(
        diagnostics[4].message,
        std::string{"name 'future' is used before its declaration"});
    TPP_CHECK_EQ(diagnostics[4].severity, DiagnosticSeverity::error);
    TPP_CHECK(diagnostics[4].primary_span.has_value());
    check_span(
        *diagnostics[4].primary_span,
        future_assignment.target.name_span);
    TPP_CHECK_EQ(diagnostics[5].message, std::string{"declaration is here"});
    TPP_CHECK_EQ(diagnostics[5].severity, DiagnosticSeverity::note);
    TPP_CHECK(diagnostics[5].primary_span.has_value());
    check_span(*diagnostics[5].primary_span, future.name_span);
}

void nearest_shadow_is_selected_and_sibling_scopes_are_isolated()
{
    ResolutionResult result{R"(int value;
int main(int parameter) {
    int value = parameter;
    value;
    {
        bool value;
        value;
    }
    value;
    if true {
        int branch;
        branch;
    } else {
        branch;
    }
}
)"};

    TPP_CHECK(!result.succeeded);
    TPP_CHECK_EQ(result.diagnostics.error_count(), std::size_t{1});

    const auto& main_function = require_function(result.program, 1);
    const auto& body = require_body(main_function);
    const auto& parameter = main_function.parameters.front();
    const auto parameter_symbol =
        require_declaration_symbol(result.declarations.symbol_for(parameter));
    const auto& function_value =
        require_statement_node<VariableDeclaration>(body, 0);
    const auto function_value_symbol = require_declaration_symbol(
        result.declarations.symbol_for(function_value));
    const auto& initializer =
        require_identifier(require_expression(function_value.initializer));
    const auto& before_block = require_expression_statement_identifier(body, 1);
    const auto& block_statement =
        require_statement_node<BlockStatement>(body, 2);
    TPP_CHECK(block_statement.block != nullptr);
    const auto& nested_block = *block_statement.block;
    const auto& nested_value =
        require_statement_node<VariableDeclaration>(nested_block, 0);
    const auto nested_value_symbol = require_declaration_symbol(
        result.declarations.symbol_for(nested_value));
    const auto& nested_use =
        require_expression_statement_identifier(nested_block, 1);
    const auto& after_block = require_expression_statement_identifier(body, 3);
    const auto& if_statement = require_statement_node<IfStatement>(body, 4);
    TPP_CHECK(if_statement.then_block != nullptr);
    TPP_CHECK(if_statement.else_block != nullptr);
    const auto& branch = require_statement_node<VariableDeclaration>(
        *if_statement.then_block,
        0);
    const auto branch_symbol =
        require_declaration_symbol(result.declarations.symbol_for(branch));
    const auto& then_use =
        require_expression_statement_identifier(*if_statement.then_block, 1);
    const auto& else_use =
        require_expression_statement_identifier(*if_statement.else_block, 0);

    TPP_CHECK_EQ(
        require_symbol_resolution(result.resolutions, initializer),
        parameter_symbol);
    TPP_CHECK_EQ(
        require_symbol_resolution(result.resolutions, before_block),
        function_value_symbol);
    TPP_CHECK_EQ(
        require_symbol_resolution(result.resolutions, nested_use),
        nested_value_symbol);
    TPP_CHECK_EQ(
        require_symbol_resolution(result.resolutions, after_block),
        function_value_symbol);
    TPP_CHECK_EQ(
        require_symbol_resolution(result.resolutions, then_use),
        branch_symbol);
    TPP_CHECK(!result.resolutions.resolution_for(else_use).has_value());
    TPP_CHECK_EQ(
        result.diagnostics.diagnostics().front().message,
        std::string{"unknown name 'branch'"});
    TPP_CHECK(result.diagnostics.diagnostics().front().primary_span.has_value());
    check_span(
        *result.diagnostics.diagnostics().front().primary_span,
        require_expression(
            require_statement_node<ExpressionStatement>(
                *if_statement.else_block,
                0).expression).span);
}

void assignment_targets_indices_and_values_resolve()
{
    ResolutionResult result{
        "int main() {"
        "vector<int> values;"
        "int first;"
        "int second;"
        "values[first][second] += values[(first + second)];"
        "}"};

    TPP_CHECK(result.succeeded);
    const auto& body = require_body(require_function(result.program, 0));
    const auto& values = require_statement_node<VariableDeclaration>(body, 0);
    const auto& first = require_statement_node<VariableDeclaration>(body, 1);
    const auto& second = require_statement_node<VariableDeclaration>(body, 2);
    const auto values_symbol =
        require_declaration_symbol(result.declarations.symbol_for(values));
    const auto first_symbol =
        require_declaration_symbol(result.declarations.symbol_for(first));
    const auto second_symbol =
        require_declaration_symbol(result.declarations.symbol_for(second));
    const auto& assignment =
        require_statement_node<AssignmentStatement>(body, 3);

    TPP_CHECK_EQ(
        require_symbol_resolution(result.resolutions, assignment.target),
        values_symbol);
    TPP_CHECK_EQ(assignment.target.indices.size(), std::size_t{2});
    TPP_CHECK_EQ(
        require_symbol_resolution(
            result.resolutions,
            require_identifier(require_expression(assignment.target.indices[0]))),
        first_symbol);
    TPP_CHECK_EQ(
        require_symbol_resolution(
            result.resolutions,
            require_identifier(require_expression(assignment.target.indices[1]))),
        second_symbol);

    const auto& value_index = require_expression_node<IndexExpression>(
        require_expression(assignment.value));
    TPP_CHECK_EQ(
        require_symbol_resolution(
            result.resolutions,
            require_identifier(require_expression(value_index.base))),
        values_symbol);
    const auto& parentheses = require_expression_node<ParenthesizedExpression>(
        require_expression(value_index.index));
    const auto& sum = require_expression_node<BinaryExpression>(
        require_expression(parentheses.expression));
    TPP_CHECK_EQ(
        require_symbol_resolution(
            result.resolutions,
            require_identifier(require_expression(sum.left))),
        first_symbol);
    TPP_CHECK_EQ(
        require_symbol_resolution(
            result.resolutions,
            require_identifier(require_expression(sum.right))),
        second_symbol);
}

void recursive_expression_nodes_are_traversed()
{
    ResolutionResult result{
        "int consume(int value) { return value; }"
        "int main() {"
        "int size;"
        "vector<int> values = vector<int>(size, -size);"
        "consume((values[size] + consume(size)));"
        "values[size].length();"
        "}"};

    TPP_CHECK(result.succeeded);
    const auto& consume = require_function(result.program, 0);
    const auto consume_symbol =
        require_declaration_symbol(result.declarations.symbol_for(consume));
    const auto& body = require_body(require_function(result.program, 1));
    const auto& size = require_statement_node<VariableDeclaration>(body, 0);
    const auto& values = require_statement_node<VariableDeclaration>(body, 1);
    const auto size_symbol =
        require_declaration_symbol(result.declarations.symbol_for(size));
    const auto values_symbol =
        require_declaration_symbol(result.declarations.symbol_for(values));

    const auto& construction =
        require_expression_node<VectorConstructionExpression>(
            require_expression(values.initializer));
    TPP_CHECK_EQ(construction.arguments.size(), std::size_t{2});
    TPP_CHECK_EQ(
        require_symbol_resolution(
            result.resolutions,
            require_identifier(require_expression(construction.arguments[0]))),
        size_symbol);
    const auto& unary = require_expression_node<UnaryExpression>(
        require_expression(construction.arguments[1]));
    TPP_CHECK_EQ(
        require_symbol_resolution(
            result.resolutions,
            require_identifier(require_expression(unary.operand))),
        size_symbol);

    const auto& call_statement =
        require_statement_node<ExpressionStatement>(body, 2);
    const auto& outer_call = require_expression_node<CallExpression>(
        require_expression(call_statement.expression));
    TPP_CHECK_EQ(
        require_symbol_resolution(
            result.resolutions,
            require_identifier(require_expression(outer_call.callee))),
        consume_symbol);
    const auto& parentheses = require_expression_node<ParenthesizedExpression>(
        require_expression(outer_call.arguments.front()));
    const auto& sum = require_expression_node<BinaryExpression>(
        require_expression(parentheses.expression));
    const auto& index = require_expression_node<IndexExpression>(
        require_expression(sum.left));
    TPP_CHECK_EQ(
        require_symbol_resolution(
            result.resolutions,
            require_identifier(require_expression(index.base))),
        values_symbol);
    TPP_CHECK_EQ(
        require_symbol_resolution(
            result.resolutions,
            require_identifier(require_expression(index.index))),
        size_symbol);
    const auto& inner_call =
        require_expression_node<CallExpression>(require_expression(sum.right));
    TPP_CHECK_EQ(
        require_symbol_resolution(
            result.resolutions,
            require_identifier(require_expression(inner_call.callee))),
        consume_symbol);
    TPP_CHECK_EQ(
        require_symbol_resolution(
            result.resolutions,
            require_identifier(require_expression(inner_call.arguments.front()))),
        size_symbol);

    const auto& member_statement =
        require_statement_node<ExpressionStatement>(body, 3);
    const auto& member_call = require_expression_node<CallExpression>(
        require_expression(member_statement.expression));
    const auto& member = require_expression_node<MemberAccessExpression>(
        require_expression(member_call.callee));
    TPP_CHECK_EQ(member.member, std::string{"length"});
    const auto& member_base =
        require_expression_node<IndexExpression>(require_expression(member.base));
    TPP_CHECK_EQ(
        require_symbol_resolution(
            result.resolutions,
            require_identifier(require_expression(member_base.base))),
        values_symbol);
    TPP_CHECK_EQ(
        require_symbol_resolution(
            result.resolutions,
            require_identifier(require_expression(member_base.index))),
        size_symbol);
}

void conditions_returns_and_nested_blocks_use_their_scopes()
{
    ResolutionResult result{
        "int main(int condition) {"
        "int value = condition;"
        "if condition { return value; } else { value; }"
        "while condition { value; }"
        "}"};

    TPP_CHECK(result.succeeded);
    const auto& function = require_function(result.program, 0);
    const auto& body = require_body(function);
    const auto parameter_symbol = require_declaration_symbol(
        result.declarations.symbol_for(function.parameters.front()));
    const auto& value = require_statement_node<VariableDeclaration>(body, 0);
    const auto value_symbol =
        require_declaration_symbol(result.declarations.symbol_for(value));
    const auto& if_statement = require_statement_node<IfStatement>(body, 1);
    const auto& while_statement = require_statement_node<WhileStatement>(body, 2);

    TPP_CHECK_EQ(
        require_symbol_resolution(
            result.resolutions,
            require_identifier(require_expression(value.initializer))),
        parameter_symbol);
    TPP_CHECK_EQ(
        require_symbol_resolution(
            result.resolutions,
            require_identifier(require_expression(if_statement.condition))),
        parameter_symbol);
    TPP_CHECK(if_statement.then_block != nullptr);
    const auto& returned = require_statement_node<ReturnStatement>(
        *if_statement.then_block,
        0);
    TPP_CHECK_EQ(
        require_symbol_resolution(
            result.resolutions,
            require_identifier(require_expression(returned.value))),
        value_symbol);
    TPP_CHECK(if_statement.else_block != nullptr);
    TPP_CHECK_EQ(
        require_symbol_resolution(
            result.resolutions,
            require_expression_statement_identifier(*if_statement.else_block, 0)),
        value_symbol);
    TPP_CHECK_EQ(
        require_symbol_resolution(
            result.resolutions,
            require_identifier(require_expression(while_statement.condition))),
        parameter_symbol);
    TPP_CHECK(while_statement.body != nullptr);
    TPP_CHECK_EQ(
        require_symbol_resolution(
            result.resolutions,
            require_expression_statement_identifier(*while_statement.body, 0)),
        value_symbol);
}

void loop_bounds_iterables_and_bindings_use_correct_scopes()
{
    ResolutionResult result{
        "int main() {"
        "int index;"
        "int begin;"
        "int end;"
        "vector<int> values;"
        "for index in begin..end { index; begin; }"
        "index;"
        "for value in values { value; values; }"
        "}"};

    TPP_CHECK(result.succeeded);
    const auto& body = require_body(require_function(result.program, 0));
    const auto& index = require_statement_node<VariableDeclaration>(body, 0);
    const auto& begin = require_statement_node<VariableDeclaration>(body, 1);
    const auto& end = require_statement_node<VariableDeclaration>(body, 2);
    const auto& values = require_statement_node<VariableDeclaration>(body, 3);
    const auto index_symbol =
        require_declaration_symbol(result.declarations.symbol_for(index));
    const auto begin_symbol =
        require_declaration_symbol(result.declarations.symbol_for(begin));
    const auto end_symbol =
        require_declaration_symbol(result.declarations.symbol_for(end));
    const auto values_symbol =
        require_declaration_symbol(result.declarations.symbol_for(values));

    const auto& range = require_statement_node<ForRangeStatement>(body, 4);
    const auto range_symbol =
        require_declaration_symbol(result.declarations.symbol_for(range));
    TPP_CHECK_EQ(
        require_symbol_resolution(
            result.resolutions,
            require_identifier(require_expression(range.begin))),
        begin_symbol);
    TPP_CHECK_EQ(
        require_symbol_resolution(
            result.resolutions,
            require_identifier(require_expression(range.end))),
        end_symbol);
    TPP_CHECK(range.body != nullptr);
    TPP_CHECK_EQ(
        require_symbol_resolution(
            result.resolutions,
            require_expression_statement_identifier(*range.body, 0)),
        range_symbol);
    TPP_CHECK_EQ(
        require_symbol_resolution(
            result.resolutions,
            require_expression_statement_identifier(*range.body, 1)),
        begin_symbol);
    TPP_CHECK_EQ(
        require_symbol_resolution(
            result.resolutions,
            require_expression_statement_identifier(body, 5)),
        index_symbol);

    const auto& each = require_statement_node<ForEachStatement>(body, 6);
    const auto each_symbol =
        require_declaration_symbol(result.declarations.symbol_for(each));
    TPP_CHECK_EQ(
        require_symbol_resolution(
            result.resolutions,
            require_identifier(require_expression(each.iterable))),
        values_symbol);
    TPP_CHECK(each.body != nullptr);
    TPP_CHECK_EQ(
        require_symbol_resolution(
            result.resolutions,
            require_expression_statement_identifier(*each.body, 0)),
        each_symbol);
    TPP_CHECK_EQ(
        require_symbol_resolution(
            result.resolutions,
            require_expression_statement_identifier(*each.body, 1)),
        values_symbol);
}

void functions_are_hoisted_in_global_and_nested_scopes()
{
    ResolutionResult result{
        "int first() { return second(); }"
        "int second() { return 0; }"
        "int outer() {"
        "return left();"
        "int left() { return right(); }"
        "int right() { return left(); }"
        "}"};

    TPP_CHECK(result.succeeded);
    const auto& first = require_function(result.program, 0);
    const auto& second = require_function(result.program, 1);
    const auto& outer = require_function(result.program, 2);
    const auto second_symbol =
        require_declaration_symbol(result.declarations.symbol_for(second));
    const auto& first_return = require_statement_node<ReturnStatement>(
        require_body(first),
        0);
    const auto& second_call = require_expression_node<CallExpression>(
        require_expression(first_return.value));
    TPP_CHECK_EQ(
        require_symbol_resolution(
            result.resolutions,
            require_identifier(require_expression(second_call.callee))),
        second_symbol);

    const auto& outer_body = require_body(outer);
    const auto& left = require_nested_function(outer_body, 1);
    const auto& right = require_nested_function(outer_body, 2);
    const auto left_symbol =
        require_declaration_symbol(result.declarations.symbol_for(left));
    const auto right_symbol =
        require_declaration_symbol(result.declarations.symbol_for(right));
    const auto& outer_return =
        require_statement_node<ReturnStatement>(outer_body, 0);
    const auto& left_call = require_expression_node<CallExpression>(
        require_expression(outer_return.value));
    TPP_CHECK_EQ(
        require_symbol_resolution(
            result.resolutions,
            require_identifier(require_expression(left_call.callee))),
        left_symbol);

    const auto& left_return = require_statement_node<ReturnStatement>(
        require_body(left),
        0);
    const auto& right_call = require_expression_node<CallExpression>(
        require_expression(left_return.value));
    TPP_CHECK_EQ(
        require_symbol_resolution(
            result.resolutions,
            require_identifier(require_expression(right_call.callee))),
        right_symbol);

    const auto& right_return = require_statement_node<ReturnStatement>(
        require_body(right),
        0);
    const auto& recursive_left_call = require_expression_node<CallExpression>(
        require_expression(right_return.value));
    TPP_CHECK_EQ(
        require_symbol_resolution(
            result.resolutions,
            require_identifier(require_expression(recursive_left_call.callee))),
        left_symbol);

}

void forbidden_captures_are_diagnosed_but_keep_symbol_mappings()
{
    ResolutionResult result{R"(int global;
int top() { return 0; }
int outer(int parameter) {
    int local;
    int nested(int own) {
        global;
        top();
        nested();
        own;
        local;
        parameter;
        return 0;
    }
    for item in 0..1 {
        int loop_user() {
            item;
            return 0;
        }
    }
    return 0;
}
)"};

    TPP_CHECK(!result.succeeded);
    TPP_CHECK_EQ(result.diagnostics.error_count(), std::size_t{3});
    TPP_CHECK_EQ(result.diagnostics.diagnostics().size(), std::size_t{6});

    const auto& global = require_global_variable(result.program, 0);
    const auto& top = require_function(result.program, 1);
    const auto& outer = require_function(result.program, 2);
    const auto& outer_body = require_body(outer);
    const auto& local = require_statement_node<VariableDeclaration>(outer_body, 0);
    const auto& nested = require_nested_function(outer_body, 1);
    const auto& nested_body = require_body(nested);
    const auto& range = require_statement_node<ForRangeStatement>(outer_body, 2);
    TPP_CHECK(range.body != nullptr);
    const auto& loop_user = require_nested_function(*range.body, 0);
    const auto& loop_user_body = require_body(loop_user);
    const auto global_symbol =
        require_declaration_symbol(result.declarations.symbol_for(global));
    const auto top_symbol =
        require_declaration_symbol(result.declarations.symbol_for(top));
    const auto nested_symbol =
        require_declaration_symbol(result.declarations.symbol_for(nested));
    const auto own_symbol = require_declaration_symbol(
        result.declarations.symbol_for(nested.parameters.front()));
    const auto local_symbol =
        require_declaration_symbol(result.declarations.symbol_for(local));
    const auto parameter_symbol = require_declaration_symbol(
        result.declarations.symbol_for(outer.parameters.front()));
    const auto item_symbol =
        require_declaration_symbol(result.declarations.symbol_for(range));

    const auto& global_use =
        require_expression_statement_identifier(nested_body, 0);
    const auto& top_statement =
        require_statement_node<ExpressionStatement>(nested_body, 1);
    const auto& top_call = require_expression_node<CallExpression>(
        require_expression(top_statement.expression));
    const auto& top_use = require_identifier(require_expression(top_call.callee));
    const auto& nested_statement =
        require_statement_node<ExpressionStatement>(nested_body, 2);
    const auto& recursive_call = require_expression_node<CallExpression>(
        require_expression(nested_statement.expression));
    const auto& nested_use =
        require_identifier(require_expression(recursive_call.callee));
    const auto& own_use =
        require_expression_statement_identifier(nested_body, 3);
    const auto& local_use =
        require_expression_statement_identifier(nested_body, 4);
    const auto& parameter_use =
        require_expression_statement_identifier(nested_body, 5);
    const auto& item_use =
        require_expression_statement_identifier(loop_user_body, 0);

    TPP_CHECK_EQ(
        require_symbol_resolution(result.resolutions, global_use),
        global_symbol);
    TPP_CHECK_EQ(
        require_symbol_resolution(result.resolutions, top_use),
        top_symbol);
    TPP_CHECK_EQ(
        require_symbol_resolution(result.resolutions, nested_use),
        nested_symbol);
    TPP_CHECK_EQ(
        require_symbol_resolution(result.resolutions, own_use),
        own_symbol);
    TPP_CHECK_EQ(
        require_symbol_resolution(result.resolutions, local_use),
        local_symbol);
    TPP_CHECK_EQ(
        require_symbol_resolution(result.resolutions, parameter_use),
        parameter_symbol);
    TPP_CHECK_EQ(
        require_symbol_resolution(result.resolutions, item_use),
        item_symbol);

    const auto diagnostics = result.diagnostics.diagnostics();
    TPP_CHECK_EQ(
        diagnostics[0].message,
        std::string{
            "nested function cannot capture 'local' from an enclosing function"});
    TPP_CHECK_EQ(diagnostics[0].severity, DiagnosticSeverity::error);
    TPP_CHECK(diagnostics[0].primary_span.has_value());
    check_span(
        *diagnostics[0].primary_span,
        require_expression(
            require_statement_node<ExpressionStatement>(nested_body, 4)
                .expression).span);
    TPP_CHECK_EQ(diagnostics[1].message, std::string{"declaration is here"});
    TPP_CHECK_EQ(diagnostics[1].severity, DiagnosticSeverity::note);
    TPP_CHECK(diagnostics[1].primary_span.has_value());
    check_span(*diagnostics[1].primary_span, local.name_span);
    TPP_CHECK_EQ(
        diagnostics[2].message,
        std::string{
            "nested function cannot capture 'parameter' from an enclosing function"});
    TPP_CHECK_EQ(diagnostics[2].severity, DiagnosticSeverity::error);
    TPP_CHECK(diagnostics[2].primary_span.has_value());
    check_span(
        *diagnostics[2].primary_span,
        require_expression(
            require_statement_node<ExpressionStatement>(nested_body, 5)
                .expression).span);
    TPP_CHECK_EQ(diagnostics[3].message, std::string{"declaration is here"});
    TPP_CHECK_EQ(diagnostics[3].severity, DiagnosticSeverity::note);
    TPP_CHECK(diagnostics[3].primary_span.has_value());
    check_span(*diagnostics[3].primary_span, outer.parameters.front().name_span);
    TPP_CHECK_EQ(
        diagnostics[4].message,
        std::string{
            "nested function cannot capture 'item' from an enclosing function"});
    TPP_CHECK_EQ(diagnostics[4].severity, DiagnosticSeverity::error);
    TPP_CHECK(diagnostics[4].primary_span.has_value());
    check_span(
        *diagnostics[4].primary_span,
        require_expression(
            require_statement_node<ExpressionStatement>(loop_user_body, 0)
                .expression).span);
    TPP_CHECK_EQ(diagnostics[5].message, std::string{"declaration is here"});
    TPP_CHECK_EQ(diagnostics[5].severity, DiagnosticSeverity::note);
    TPP_CHECK(diagnostics[5].primary_span.has_value());
    check_span(*diagnostics[5].primary_span, range.variable_span);
}

void builtins_are_explicit_and_lexical_declarations_shadow_them()
{
    ResolutionResult result{
        "int main() {"
        "print(0);"
        "read_int();"
        "read_string();"
        "read_char();"
        "len(\"text\");"
        "int len;"
        "len;"
        "substring(\"text\", 0, 1);"
        "string text;"
        "text.length();"
        "text.push('x');"
        "}"};

    TPP_CHECK(result.succeeded);
    const auto& body = require_body(require_function(result.program, 0));
    const auto& len = require_statement_node<VariableDeclaration>(body, 5);
    const auto len_symbol =
        require_declaration_symbol(result.declarations.symbol_for(len));

    const auto expected_builtins = std::vector<BuiltinFunctionKind>{
        BuiltinFunctionKind::print,
        BuiltinFunctionKind::read_int,
        BuiltinFunctionKind::read_string,
        BuiltinFunctionKind::read_char,
    };
    for (std::size_t index = 0; index < expected_builtins.size(); ++index) {
        const auto& statement =
            require_statement_node<ExpressionStatement>(body, index);
        const auto& call = require_expression_node<CallExpression>(
            require_expression(statement.expression));
        const auto& callee = require_identifier(require_expression(call.callee));
        TPP_CHECK_EQ(
            require_builtin_resolution(result.resolutions, callee),
            expected_builtins[index]);
    }

    const auto& len_call_statement =
        require_statement_node<ExpressionStatement>(body, 4);
    const auto& len_call = require_expression_node<CallExpression>(
        require_expression(len_call_statement.expression));
    const auto& builtin_len =
        require_identifier(require_expression(len_call.callee));
    TPP_CHECK_EQ(
        require_builtin_resolution(result.resolutions, builtin_len),
        BuiltinFunctionKind::len);

    const auto& len_use = require_expression_statement_identifier(body, 6);
    TPP_CHECK_EQ(
        require_symbol_resolution(result.resolutions, len_use),
        len_symbol);

    const auto& substring_statement =
        require_statement_node<ExpressionStatement>(body, 7);
    const auto& substring_call = require_expression_node<CallExpression>(
        require_expression(substring_statement.expression));
    const auto& substring =
        require_identifier(require_expression(substring_call.callee));
    TPP_CHECK_EQ(
        require_builtin_resolution(result.resolutions, substring),
        BuiltinFunctionKind::substring);

    const auto& text = require_statement_node<VariableDeclaration>(body, 8);
    const auto text_symbol =
        require_declaration_symbol(result.declarations.symbol_for(text));
    const auto& member_statement =
        require_statement_node<ExpressionStatement>(body, 9);
    const auto& member_call = require_expression_node<CallExpression>(
        require_expression(member_statement.expression));
    const auto& member = require_expression_node<MemberAccessExpression>(
        require_expression(member_call.callee));
    TPP_CHECK_EQ(member.member, std::string{"length"});
    TPP_CHECK_EQ(
        require_symbol_resolution(
            result.resolutions,
            require_identifier(require_expression(member.base))),
        text_symbol);

    const auto& push_statement =
        require_statement_node<ExpressionStatement>(body, 10);
    const auto& push_call = require_expression_node<CallExpression>(
        require_expression(push_statement.expression));
    const auto& push = require_expression_node<MemberAccessExpression>(
        require_expression(push_call.callee));
    TPP_CHECK_EQ(push.member, std::string{"push"});
    TPP_CHECK_EQ(
        require_symbol_resolution(
            result.resolutions,
            require_identifier(require_expression(push.base))),
        text_symbol);

    const auto builtin_names = std::vector<
        std::pair<std::string_view, BuiltinFunctionKind>>{
        {"print", BuiltinFunctionKind::print},
        {"read_int", BuiltinFunctionKind::read_int},
        {"read_string", BuiltinFunctionKind::read_string},
        {"read_char", BuiltinFunctionKind::read_char},
        {"len", BuiltinFunctionKind::len},
        {"substring", BuiltinFunctionKind::substring},
    };
    for (const auto& [name, kind] : builtin_names) {
        TPP_CHECK_EQ(
            tpp::find_builtin_function(name),
            std::optional<BuiltinFunctionKind>{kind});
    }
    TPP_CHECK(!tpp::find_builtin_function("length").has_value());
    TPP_CHECK(!tpp::find_builtin_function("unknown").has_value());
}

void unknown_names_recover_without_duplicate_diagnostics()
{
    ResolutionResult result{R"(int main() {
    int index;
    missing[0].length();
    another;
    missing_target[index] = 0;
}
)"};

    TPP_CHECK(!result.succeeded);
    TPP_CHECK_EQ(result.diagnostics.error_count(), std::size_t{3});
    TPP_CHECK_EQ(result.diagnostics.diagnostics().size(), std::size_t{3});
    const auto& body = require_body(require_function(result.program, 0));

    const auto& first_statement =
        require_statement_node<ExpressionStatement>(body, 1);
    const auto& call = require_expression_node<CallExpression>(
        require_expression(first_statement.expression));
    const auto& member = require_expression_node<MemberAccessExpression>(
        require_expression(call.callee));
    TPP_CHECK_EQ(member.member, std::string{"length"});
    const auto& index = require_expression_node<IndexExpression>(
        require_expression(member.base));
    const auto& missing = require_identifier(require_expression(index.base));
    const auto& another = require_expression_statement_identifier(body, 2);
    const auto& assignment =
        require_statement_node<AssignmentStatement>(body, 3);

    TPP_CHECK(!result.resolutions.resolution_for(missing).has_value());
    TPP_CHECK(!result.resolutions.resolution_for(another).has_value());
    TPP_CHECK(!result.resolutions.resolution_for(assignment.target).has_value());
    const auto diagnostics = result.diagnostics.diagnostics();
    TPP_CHECK_EQ(diagnostics[0].message, std::string{"unknown name 'missing'"});
    TPP_CHECK_EQ(diagnostics[0].severity, DiagnosticSeverity::error);
    TPP_CHECK(diagnostics[0].primary_span.has_value());
    check_span(*diagnostics[0].primary_span, require_expression(index.base).span);
    TPP_CHECK_EQ(diagnostics[1].message, std::string{"unknown name 'another'"});
    TPP_CHECK_EQ(diagnostics[1].severity, DiagnosticSeverity::error);
    TPP_CHECK(diagnostics[1].primary_span.has_value());
    check_span(
        *diagnostics[1].primary_span,
        require_expression(
            require_statement_node<ExpressionStatement>(body, 2).expression).span);
    TPP_CHECK_EQ(
        diagnostics[2].message,
        std::string{"unknown name 'missing_target'"});
    TPP_CHECK_EQ(diagnostics[2].severity, DiagnosticSeverity::error);
    TPP_CHECK(diagnostics[2].primary_span.has_value());
    check_span(*diagnostics[2].primary_span, assignment.target.name_span);
}

void fresh_resolution_info_has_no_foreign_mappings()
{
    ResolutionInfo resolutions;
    TPP_CHECK(resolutions.empty());

    const IdentifierExpression identifier{.name = "value"};
    const AssignmentTarget assignment{
        .span = SourceSpan{SourceId{0}, 0, 5},
        .name = "value",
        .name_span = SourceSpan{SourceId{0}, 0, 5},
        .indices = {},
    };
    TPP_CHECK(!resolutions.resolution_for(identifier).has_value());
    TPP_CHECK(!resolutions.resolution_for(assignment).has_value());
}

}

int main()
{
    return tpp::test::run({
        {"globals parameters locals and initializers",
         globals_parameters_locals_and_initializers_resolve},
        {"ordered locals and fallback",
         locals_are_ordered_with_outer_and_builtin_fallbacks},
        {"nearest shadow and sibling isolation",
         nearest_shadow_is_selected_and_sibling_scopes_are_isolated},
        {"assignment targets indices and values",
         assignment_targets_indices_and_values_resolve},
        {"recursive expression traversal",
         recursive_expression_nodes_are_traversed},
        {"conditions returns and nested blocks",
         conditions_returns_and_nested_blocks_use_their_scopes},
        {"loop scopes and visibility",
         loop_bounds_iterables_and_bindings_use_correct_scopes},
        {"function hoisting", functions_are_hoisted_in_global_and_nested_scopes},
        {"forbidden captures retain mappings",
         forbidden_captures_are_diagnosed_but_keep_symbol_mappings},
        {"builtins and lexical shadowing",
         builtins_are_explicit_and_lexical_declarations_shadow_them},
        {"unknown name recovery",
         unknown_names_recover_without_duplicate_diagnostics},
        {"fresh resolution info", fresh_resolution_info_has_no_foreign_mappings},
    });
}

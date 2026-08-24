#include "test_support.hpp"

#include "pseudo/ast/program.hpp"
#include "pseudo/diagnostics/diagnostic.hpp"
#include "pseudo/diagnostics/diagnostic_engine.hpp"
#include "pseudo/lexer/lexer.hpp"
#include "pseudo/parser/parser.hpp"
#include "pseudo/semantic/control_flow_checker.hpp"
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
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

using tpp::Block;
using tpp::BlockStatement;
using tpp::ControlFlowChecker;
using tpp::DeclarationCollector;
using tpp::DeclarationInfo;
using tpp::Diagnostic;
using tpp::DiagnosticEngine;
using tpp::DiagnosticSeverity;
using tpp::ForEachStatement;
using tpp::ForRangeStatement;
using tpp::FunctionDeclaration;
using tpp::IfStatement;
using tpp::Lexer;
using tpp::NameResolver;
using tpp::Parser;
using tpp::Program;
using tpp::ResolutionInfo;
using tpp::SourceId;
using tpp::SourceManager;
using tpp::SourceSpan;
using tpp::Statement;
using tpp::SymbolTable;
using tpp::Token;
using tpp::TypeChecker;
using tpp::TypeContext;
using tpp::TypeInfo;
using tpp::WhileStatement;

class FlowResult {
public:
    explicit FlowResult(std::string contents)
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

        TypeChecker type_checker{
            types,
            symbols,
            declarations,
            resolutions,
            type_info,
            diagnostics,
        };
        TPP_CHECK(type_checker.check(program));
        TPP_CHECK(!diagnostics.has_errors());
    }

    bool check()
    {
        ControlFlowChecker checker{diagnostics};
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

FunctionDeclaration& require_function(Program& program, const std::size_t index)
{
    TPP_CHECK(index < program.declarations.size());
    return require_variant<FunctionDeclaration>(program.declarations[index]);
}

Block& require_body(FunctionDeclaration& function)
{
    TPP_CHECK(function.body != nullptr);
    return *function.body;
}

const Block& require_body(const FunctionDeclaration& function)
{
    TPP_CHECK(function.body != nullptr);
    return *function.body;
}

Statement& require_statement(Block& block, const std::size_t index)
{
    TPP_CHECK(index < block.items.size());
    return require_variant<Statement>(block.items[index]);
}

const Statement& require_statement(const Block& block, const std::size_t index)
{
    TPP_CHECK(index < block.items.size());
    return require_variant<Statement>(block.items[index]);
}

template <typename Node>
Node& require_statement_node(Block& block, const std::size_t index)
{
    return require_variant<Node>(require_statement(block, index).node);
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

const Diagnostic& require_diagnostic(
    const FlowResult& result,
    const std::size_t index)
{
    const auto diagnostics = result.diagnostics.diagnostics();
    TPP_CHECK(index < diagnostics.size());
    return diagnostics[index];
}

void check_span(const SourceSpan actual, const SourceSpan expected)
{
    TPP_CHECK_EQ(actual.source, expected.source);
    TPP_CHECK_EQ(actual.begin, expected.begin);
    TPP_CHECK_EQ(actual.end, expected.end);
}

void empty_program_void_functions_and_main_may_fall_through()
{
    FlowResult empty{""};
    TPP_CHECK(empty.check());
    TPP_CHECK(empty.diagnostics.diagnostics().empty());

    FlowResult functions{R"(void helper() {}
int main() {}
)"};
    TPP_CHECK(functions.check());
    TPP_CHECK(functions.diagnostics.diagnostics().empty());
}

void main_fallthrough_exemption_is_exact()
{
    FlowResult parameterized{"int main(int value) {}"};
    TPP_CHECK(!parameterized.check());
    TPP_CHECK_EQ(parameterized.diagnostics.error_count(), std::size_t{1});
    TPP_CHECK_EQ(
        require_diagnostic(parameterized, 0).message,
        std::string{
            "non-void function 'main' may reach the end without returning "
            "a value"});

    FlowResult non_integer{"bool main() {}"};
    TPP_CHECK(!non_integer.check());
    TPP_CHECK_EQ(non_integer.diagnostics.error_count(), std::size_t{1});

    FlowResult nested{R"(void outer() {
    int main() {}
}
)"};
    TPP_CHECK(!nested.check());
    TPP_CHECK_EQ(nested.diagnostics.error_count(), std::size_t{1});
    TPP_CHECK_EQ(
        require_diagnostic(nested, 0).message,
        std::string{
            "non-void function 'main' may reach the end without returning "
            "a value"});
}

void direct_and_nested_block_returns_terminate_non_void_functions()
{
    FlowResult result{R"(int direct() { return 1; }
int nested() { { { return 2; } } }
)"};

    TPP_CHECK(result.check());
    TPP_CHECK(result.diagnostics.diagnostics().empty());
}

void exhaustive_if_requires_both_returning_branches()
{
    FlowResult result{R"(int choose(bool condition) {
    if condition { return 1; } else { return 2; }
}
int nested(bool first, bool second) {
    if first {
        if second { return 1; } else { return 2; }
    } else {
        return 3;
    }
}
)"};

    TPP_CHECK(result.check());
    TPP_CHECK(result.diagnostics.diagnostics().empty());
}

void missing_else_and_partial_branch_report_the_function_name()
{
    FlowResult result{R"(int missing_else(bool condition) {
    if condition { return 1; }
}
int partial(bool condition) {
    if condition { return 1; } else { print(0); }
}
)"};

    TPP_CHECK(!result.check());
    TPP_CHECK_EQ(result.diagnostics.error_count(), std::size_t{2});
    TPP_CHECK_EQ(result.diagnostics.diagnostics().size(), std::size_t{2});
    for (std::size_t index = 0; index < 2; ++index) {
        const auto& function = require_function(result.program, index);
        const auto& diagnostic = require_diagnostic(result, index);
        TPP_CHECK_EQ(diagnostic.severity, DiagnosticSeverity::error);
        TPP_CHECK_EQ(
            diagnostic.message,
            std::string{"non-void function '"} + function.name
                + "' may reach the end without returning a value");
        TPP_CHECK(diagnostic.primary_span.has_value());
        check_span(*diagnostic.primary_span, function.name_span);
    }
}

void return_after_partial_if_completes_the_function()
{
    FlowResult result{R"(int value(bool condition) {
    if condition { return 1; }
    return 2;
}
)"};
    TPP_CHECK(result.check());
}

void all_loops_are_conservatively_fallthrough_even_when_condition_is_true()
{
    FlowResult result{R"(int while_loop() { while true {} }
int range_loop() { for i in 0..1 {} }
int each_loop() {
    vector<int> values;
    for value in values {}
}
)"};

    TPP_CHECK(!result.check());
    TPP_CHECK_EQ(result.diagnostics.error_count(), std::size_t{3});
    for (std::size_t index = 0; index < 3; ++index) {
        const auto& function = require_function(result.program, index);
        const auto& diagnostic = require_diagnostic(result, index);
        TPP_CHECK_EQ(diagnostic.severity, DiagnosticSeverity::error);
        TPP_CHECK(diagnostic.primary_span.has_value());
        check_span(*diagnostic.primary_span, function.name_span);
    }
}

void returns_only_inside_loops_do_not_complete_a_non_void_function()
{
    FlowResult result{R"(int while_loop() { while true { return 1; } }
int range_loop() { for i in 0..1 { return i; } }
int each_loop() {
    string text = "x";
    for ch in text { return 1; }
}
)"};

    TPP_CHECK(!result.check());
    TPP_CHECK_EQ(result.diagnostics.error_count(), std::size_t{3});
}

void return_after_each_loop_completes_the_function()
{
    FlowResult result{R"(int value() {
    while false {}
    for i in 0..1 {}
    string text = "x";
    for ch in text {}
    return 0;
}
)"};
    TPP_CHECK(result.check());
}

void break_and_continue_are_legal_in_all_loop_kinds_and_nested_blocks()
{
    FlowResult result{R"(void loops(bool condition) {
    while condition {
        if condition { { break; } }
        { continue; }
    }
    for i in 0..1 {
        if condition { break; }
        continue;
    }
    string text = "x";
    for ch in text {
        { break; }
        continue;
    }
}
)"};

    TPP_CHECK(result.check());
    TPP_CHECK_EQ(result.diagnostics.error_count(), std::size_t{0});
    TPP_CHECK_EQ(result.diagnostics.diagnostics().size(), std::size_t{1});
    TPP_CHECK_EQ(
        require_diagnostic(result, 0).severity,
        DiagnosticSeverity::warning);
    TPP_CHECK_EQ(
        require_diagnostic(result, 0).message,
        std::string{"unreachable statement"});
}

void exhaustive_transfer_branches_terminate_the_loop_body_path()
{
    FlowResult result{R"(void choose(bool condition) {
    while condition {
        if condition { break; } else { continue; }
        print(1);
    }
}
)"};

    TPP_CHECK(result.check());
    TPP_CHECK_EQ(result.diagnostics.error_count(), std::size_t{0});
    TPP_CHECK_EQ(result.diagnostics.diagnostics().size(), std::size_t{1});

    const auto& function = require_function(result.program, 0);
    const auto& body = require_body(function);
    const auto& loop = require_statement_node<WhileStatement>(body, 0);
    TPP_CHECK(loop.body != nullptr);
    const auto& diagnostic = require_diagnostic(result, 0);
    TPP_CHECK_EQ(diagnostic.severity, DiagnosticSeverity::warning);
    TPP_CHECK(diagnostic.primary_span.has_value());
    check_span(
        *diagnostic.primary_span,
        require_statement(*loop.body, 1).span);
}

void break_and_continue_outside_loops_are_errors_with_statement_spans()
{
    FlowResult result{R"(void invalid() {
    break;
    continue;
    { break; }
    if true { continue; }
}
)"};

    TPP_CHECK(!result.check());
    TPP_CHECK_EQ(result.diagnostics.error_count(), std::size_t{4});
    const auto& body = require_body(require_function(result.program, 0));
    const auto& nested = require_statement_node<BlockStatement>(body, 2);
    TPP_CHECK(nested.block != nullptr);
    const auto& condition = require_statement_node<IfStatement>(body, 3);
    TPP_CHECK(condition.then_block != nullptr);
    const std::vector<const Statement*> statements{
        &require_statement(body, 0),
        &require_statement(body, 1),
        &require_statement(*nested.block, 0),
        &require_statement(*condition.then_block, 0),
    };
    const std::vector<std::string> messages{
        "'break' is only allowed inside a loop",
        "'continue' is only allowed inside a loop",
        "'break' is only allowed inside a loop",
        "'continue' is only allowed inside a loop",
    };
    for (std::size_t index = 0; index < statements.size(); ++index) {
        const auto& diagnostic = require_diagnostic(result, index);
        TPP_CHECK_EQ(diagnostic.severity, DiagnosticSeverity::error);
        TPP_CHECK_EQ(diagnostic.message, messages[index]);
        TPP_CHECK(diagnostic.primary_span.has_value());
        check_span(*diagnostic.primary_span, statements[index]->span);
    }
}

void nested_loop_depth_and_function_boundaries_are_independent()
{
    FlowResult result{R"(void outer() {
    while true {
        for i in 0..1 {
            break;
        }
        void nested_invalid() { break; continue; }
        void nested_valid() { while true { break; continue; } }
        break;
    }
}
)"};

    TPP_CHECK(!result.check());
    TPP_CHECK_EQ(result.diagnostics.error_count(), std::size_t{2});
    TPP_CHECK_EQ(
        require_diagnostic(result, 0).message,
        std::string{"'break' is only allowed inside a loop"});
    TPP_CHECK_EQ(
        require_diagnostic(result, 1).message,
        std::string{"'continue' is only allowed inside a loop"});
}

void unreachable_runtime_suffix_warns_once_at_its_first_statement()
{
    FlowResult result{R"(void after_return() {
    return;
    print(1);
    print(2);
    int value = 3;
}
)"};

    TPP_CHECK(result.check());
    TPP_CHECK_EQ(result.diagnostics.error_count(), std::size_t{0});
    TPP_CHECK_EQ(result.diagnostics.diagnostics().size(), std::size_t{1});
    const auto& body = require_body(require_function(result.program, 0));
    const auto& diagnostic = require_diagnostic(result, 0);
    TPP_CHECK_EQ(diagnostic.severity, DiagnosticSeverity::warning);
    TPP_CHECK_EQ(diagnostic.message, std::string{"unreachable statement"});
    TPP_CHECK(diagnostic.primary_span.has_value());
    check_span(*diagnostic.primary_span, require_statement(body, 1).span);
}

void break_and_continue_make_following_loop_body_statements_unreachable()
{
    FlowResult result{R"(void loops() {
    while true { break; print(1); print(2); }
    for i in 0..1 { continue; print(i); }
}
)"};

    TPP_CHECK(result.check());
    TPP_CHECK_EQ(result.diagnostics.error_count(), std::size_t{0});
    TPP_CHECK_EQ(result.diagnostics.diagnostics().size(), std::size_t{2});
    const auto& body = require_body(require_function(result.program, 0));
    auto& while_loop = require_statement_node<WhileStatement>(body, 0);
    auto& range_loop = require_statement_node<ForRangeStatement>(body, 1);
    TPP_CHECK(while_loop.body != nullptr);
    TPP_CHECK(range_loop.body != nullptr);
    check_span(
        *require_diagnostic(result, 0).primary_span,
        require_statement(*while_loop.body, 1).span);
    check_span(
        *require_diagnostic(result, 1).primary_span,
        require_statement(*range_loop.body, 1).span);
}

void terminal_compound_statements_propagate_flow_to_the_parent_block()
{
    FlowResult result{R"(void blocks(bool condition) {
    { return; }
    print(1);
}
void branches(bool condition) {
    if condition { return; } else { return; }
    print(2);
}
)"};

    TPP_CHECK(result.check());
    TPP_CHECK_EQ(result.diagnostics.diagnostics().size(), std::size_t{2});
    for (std::size_t index = 0; index < 2; ++index) {
        const auto& body = require_body(require_function(result.program, index));
        const auto& diagnostic = require_diagnostic(result, index);
        TPP_CHECK_EQ(diagnostic.severity, DiagnosticSeverity::warning);
        check_span(*diagnostic.primary_span, require_statement(body, 1).span);
    }
}

void partial_if_and_loops_do_not_create_false_unreachable_warnings()
{
    FlowResult result{R"(void flow(bool condition) {
    if condition { return; }
    print(1);
    while condition { return; }
    print(2);
    for i in 0..1 { return; }
    print(3);
}
)"};

    TPP_CHECK(result.check());
    TPP_CHECK(result.diagnostics.diagnostics().empty());
}

void nested_functions_are_checked_independently_and_do_not_reset_a_suffix()
{
    FlowResult result{R"(void outer() {
    return;
    int nested_missing() {}
    void nested_invalid() { break; }
    print(1);
}
)"};

    TPP_CHECK(!result.check());
    TPP_CHECK_EQ(result.diagnostics.error_count(), std::size_t{2});
    TPP_CHECK_EQ(result.diagnostics.diagnostics().size(), std::size_t{3});
    const auto& outer = require_function(result.program, 0);
    const auto& body = require_body(outer);
    const auto& missing = require_nested_function(body, 1);
    TPP_CHECK_EQ(
        require_diagnostic(result, 0).message,
        std::string{"non-void function 'nested_missing' may reach the end "
                    "without returning a value"});
    check_span(*require_diagnostic(result, 0).primary_span, missing.name_span);
    TPP_CHECK_EQ(
        require_diagnostic(result, 1).message,
        std::string{"'break' is only allowed inside a loop"});
    TPP_CHECK_EQ(require_diagnostic(result, 2).severity, DiagnosticSeverity::warning);
    check_span(
        *require_diagnostic(result, 2).primary_span,
        require_statement(body, 3).span);
}

void unreachable_compound_suppresses_descendant_warnings_but_validates_errors()
{
    FlowResult result{R"(void inspect() {
    return;
    if true {
        print(1);
        break;
        print(2);
    } else {
        continue;
        print(3);
    }
    print(4);
}
)"};

    TPP_CHECK(!result.check());
    TPP_CHECK_EQ(result.diagnostics.error_count(), std::size_t{2});
    TPP_CHECK_EQ(result.diagnostics.diagnostics().size(), std::size_t{3});
    const auto& body = require_body(require_function(result.program, 0));
    TPP_CHECK_EQ(require_diagnostic(result, 0).severity, DiagnosticSeverity::warning);
    check_span(*require_diagnostic(result, 0).primary_span, require_statement(body, 1).span);
    TPP_CHECK_EQ(
        require_diagnostic(result, 1).message,
        std::string{"'break' is only allowed inside a loop"});
    TPP_CHECK_EQ(
        require_diagnostic(result, 2).message,
        std::string{"'continue' is only allowed inside a loop"});
}

void independent_flow_errors_continue_across_functions()
{
    FlowResult result{R"(int first() {}
void second() { break; }
int third(bool condition) { if condition { return 1; } }
void fourth() { continue; }
)"};

    TPP_CHECK(!result.check());
    TPP_CHECK_EQ(result.diagnostics.error_count(), std::size_t{4});
    TPP_CHECK_EQ(result.diagnostics.diagnostics().size(), std::size_t{4});
}

void malformed_function_body_is_reported_without_missing_return_cascade()
{
    FlowResult result{"int value() { return 1; }"};
    auto& function = require_function(result.program, 0);
    const auto span = function.span;
    function.body.reset();

    TPP_CHECK(!result.check());
    TPP_CHECK_EQ(result.diagnostics.error_count(), std::size_t{1});
    const auto& diagnostic = require_diagnostic(result, 0);
    TPP_CHECK_EQ(
        diagnostic.message,
        std::string{"malformed AST: function is missing a body"});
    TPP_CHECK(diagnostic.primary_span.has_value());
    check_span(*diagnostic.primary_span, span);
}

void malformed_block_statement_and_if_blocks_are_reported_without_cascades()
{
    FlowResult result{R"(int value(bool condition) {
    {}
    if condition {} else {}
}
)"};
    auto& body = require_body(require_function(result.program, 0));
    auto& block = require_statement_node<BlockStatement>(body, 0);
    auto& condition = require_statement_node<IfStatement>(body, 1);
    block.block.reset();
    condition.then_block.reset();

    TPP_CHECK(!result.check());
    TPP_CHECK_EQ(result.diagnostics.error_count(), std::size_t{2});
    TPP_CHECK_EQ(
        require_diagnostic(result, 0).message,
        std::string{"malformed AST: block statement is missing a block"});
    TPP_CHECK_EQ(
        require_diagnostic(result, 1).message,
        std::string{"malformed AST: if statement is missing a then block"});
    check_span(*require_diagnostic(result, 0).primary_span, require_statement(body, 0).span);
    check_span(*require_diagnostic(result, 1).primary_span, require_statement(body, 1).span);
}

void malformed_loop_bodies_are_reported_without_crashing()
{
    FlowResult result{R"(void loops() {
    while true {}
    for i in 0..1 {}
    string text = "x";
    for ch in text {}
}
)"};
    auto& body = require_body(require_function(result.program, 0));
    require_statement_node<WhileStatement>(body, 0).body.reset();
    require_statement_node<ForRangeStatement>(body, 1).body.reset();
    require_statement_node<ForEachStatement>(body, 3).body.reset();

    TPP_CHECK(!result.check());
    TPP_CHECK_EQ(result.diagnostics.error_count(), std::size_t{3});
    const std::vector<std::string> messages{
        "malformed AST: while statement is missing a body",
        "malformed AST: for-range statement is missing a body",
        "malformed AST: for-each statement is missing a body",
    };
    const std::vector<std::size_t> statement_indices{0, 1, 3};
    for (std::size_t index = 0; index < messages.size(); ++index) {
        const auto& diagnostic = require_diagnostic(result, index);
        TPP_CHECK_EQ(diagnostic.message, messages[index]);
        TPP_CHECK(diagnostic.primary_span.has_value());
        check_span(
            *diagnostic.primary_span,
            require_statement(body, statement_indices[index]).span);
    }
}

}

int main()
{
    return tpp::test::run({
        {"empty void and main fallthrough",
         empty_program_void_functions_and_main_may_fall_through},
        {"exact main fallthrough exemption",
         main_fallthrough_exemption_is_exact},
        {"direct and nested returns",
         direct_and_nested_block_returns_terminate_non_void_functions},
        {"exhaustive if", exhaustive_if_requires_both_returning_branches},
        {"partial if missing return",
         missing_else_and_partial_branch_report_the_function_name},
        {"return after partial if", return_after_partial_if_completes_the_function},
        {"loops conservatively fall through",
         all_loops_are_conservatively_fallthrough_even_when_condition_is_true},
        {"returns only inside loops",
         returns_only_inside_loops_do_not_complete_a_non_void_function},
        {"return after loops", return_after_each_loop_completes_the_function},
        {"legal loop transfers",
         break_and_continue_are_legal_in_all_loop_kinds_and_nested_blocks},
        {"exhaustive loop transfer branches",
         exhaustive_transfer_branches_terminate_the_loop_body_path},
        {"illegal loop transfers",
         break_and_continue_outside_loops_are_errors_with_statement_spans},
        {"nested loops and function boundary",
         nested_loop_depth_and_function_boundaries_are_independent},
        {"unreachable suffix granularity",
         unreachable_runtime_suffix_warns_once_at_its_first_statement},
        {"unreachable after loop transfers",
         break_and_continue_make_following_loop_body_statements_unreachable},
        {"terminal compound propagation",
         terminal_compound_statements_propagate_flow_to_the_parent_block},
        {"partial flow remains reachable",
         partial_if_and_loops_do_not_create_false_unreachable_warnings},
        {"nested functions in unreachable suffix",
         nested_functions_are_checked_independently_and_do_not_reset_a_suffix},
        {"unreachable descendant validation",
         unreachable_compound_suppresses_descendant_warnings_but_validates_errors},
        {"independent flow errors",
         independent_flow_errors_continue_across_functions},
        {"malformed function body",
         malformed_function_body_is_reported_without_missing_return_cascade},
        {"malformed block and if",
         malformed_block_statement_and_if_blocks_are_reported_without_cascades},
        {"malformed loop bodies",
         malformed_loop_bodies_are_reported_without_crashing},
    });
}

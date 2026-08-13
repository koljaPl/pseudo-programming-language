#include "test_support.hpp"

#include "pseudo/ast/program.hpp"
#include "pseudo/driver/compilation_session.hpp"
#include "pseudo/driver/compiler.hpp"
#include "pseudo/lexer/token.hpp"
#include "pseudo/semantic/builtin.hpp"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

#ifndef TPP_TEST_DATA_DIR
#error "TPP_TEST_DATA_DIR must point to the tests/data directory"
#endif

namespace {

std::filesystem::path data_path(const std::string& filename)
{
    return std::filesystem::path{TPP_TEST_DATA_DIR} / filename;
}

template <typename Callable>
void require_out_of_range(Callable&& callable)
{
    try {
        std::forward<Callable>(callable)();
    } catch (const std::out_of_range&) {
        return;
    }

    throw tpp::test::Failure{"expected std::out_of_range"};
}

void check_fresh_semantic_state(const tpp::CompilationSession& session)
{
    TPP_CHECK_EQ(session.types().type_count(), std::size_t{5});
    TPP_CHECK(session.types().lookup(session.types().integer_type()).has_value());
    TPP_CHECK(session.types().lookup(session.types().boolean_type()).has_value());
    TPP_CHECK(session.types().lookup(session.types().character_type()).has_value());
    TPP_CHECK(session.types().lookup(session.types().string_type()).has_value());
    TPP_CHECK(session.types().lookup(session.types().void_type()).has_value());

    TPP_CHECK_EQ(session.symbols().scope_count(), std::size_t{1});
    TPP_CHECK_EQ(session.symbols().symbol_count(), std::size_t{0});
    TPP_CHECK_EQ(session.symbols().global_scope(), tpp::ScopeId{0});
    TPP_CHECK(session.declarations().empty());
    TPP_CHECK(session.resolutions().empty());
    TPP_CHECK(session.type_info().empty());
}

void check_resolution(
    const tpp::ResolutionInfo& resolutions,
    const tpp::IdentifierExpression& reference,
    const tpp::ResolutionTarget expected)
{
    const auto actual = resolutions.resolution_for(reference);
    TPP_CHECK(actual.has_value());
    TPP_CHECK_EQ(*actual, expected);
}

void empty_source_produces_only_eof()
{
    tpp::CompilationSession session;
    const tpp::Compiler compiler;

    const bool succeeded = compiler.compile(data_path("empty.tpp"), session);

    TPP_CHECK(succeeded);
    TPP_CHECK(!session.diagnostics().has_errors());
    TPP_CHECK(session.diagnostics().diagnostics().empty());
    TPP_CHECK_EQ(session.tokens().size(), std::size_t{1});

    const auto& eof = session.tokens().front();
    TPP_CHECK_EQ(eof.kind, tpp::TokenKind::end_of_file);
    TPP_CHECK_EQ(eof.span.source.value, std::size_t{0});
    TPP_CHECK_EQ(eof.span.begin, std::size_t{0});
    TPP_CHECK_EQ(eof.span.end, std::size_t{0});
    TPP_CHECK(session.program().has_value());
    TPP_CHECK(session.program()->declarations.empty());
    TPP_CHECK(session.program()->span.empty());
    check_fresh_semantic_state(session);
}

void valid_source_produces_tokens_without_diagnostics()
{
    tpp::CompilationSession session;
    const tpp::Compiler compiler;

    const bool succeeded =
        compiler.compile(data_path("valid_lexical.tpp"), session);

    TPP_CHECK(succeeded);
    TPP_CHECK(!session.diagnostics().has_errors());
    TPP_CHECK(session.diagnostics().diagnostics().empty());
    TPP_CHECK_EQ(session.tokens().size(), std::size_t{10});

    const auto& tokens = session.tokens();
    TPP_CHECK_EQ(tokens.front().kind, tpp::TokenKind::keyword_int);
    TPP_CHECK_EQ(tokens.front().span.begin, std::size_t{0});
    TPP_CHECK_EQ(tokens.front().span.end, std::size_t{3});
    TPP_CHECK_EQ(
        session.sources().slice(tokens.front().span),
        std::string_view{"int"});

    const auto& eof = tokens.back();
    TPP_CHECK_EQ(eof.kind, tpp::TokenKind::end_of_file);
    TPP_CHECK(eof.span.empty());
    TPP_CHECK_EQ(
        eof.span.begin,
        session.sources().contents(eof.span.source).size());

    TPP_CHECK(session.program().has_value());
    TPP_CHECK_EQ(
        session.program()->declarations.size(),
        std::size_t{1});
    const auto* function = std::get_if<tpp::FunctionDeclaration>(
        &session.program()->declarations.front());
    TPP_CHECK(function != nullptr);
    TPP_CHECK_EQ(function->name, std::string{"main"});
    TPP_CHECK(function->body != nullptr);

    const auto symbol = session.declarations().symbol_for(*function);
    const auto function_scope = session.declarations().scope_for(*function->body);
    TPP_CHECK(symbol.has_value());
    TPP_CHECK(function_scope.has_value());
    TPP_CHECK_EQ(
        session.symbols().lookup_local(
            session.symbols().global_scope(),
            "main"),
        symbol);
    TPP_CHECK_EQ(
        session.symbols().scope(*function_scope).parent(),
        std::optional<tpp::ScopeId>{session.symbols().global_scope()});

    const auto* function_symbol = std::get_if<tpp::FunctionSymbol>(
        &session.symbols().symbol(*symbol).data);
    TPP_CHECK(function_symbol != nullptr);
    TPP_CHECK_EQ(
        function_symbol->return_type,
        session.types().integer_type());
    TPP_CHECK(function_symbol->parameter_types.empty());

    TPP_CHECK(function->body != nullptr);
    TPP_CHECK_EQ(function->body->items.size(), std::size_t{1});
    const auto& statement =
        std::get<tpp::Statement>(function->body->items.front());
    const auto& return_statement =
        std::get<tpp::ReturnStatement>(statement.node);
    TPP_CHECK(return_statement.value != nullptr);
    TPP_CHECK_EQ(
        session.type_info().type_of(*return_statement.value),
        std::optional<tpp::TypeId>{session.types().integer_type()});
}

void lexical_error_fails_but_recovers_to_eof()
{
    tpp::CompilationSession session;
    const tpp::Compiler compiler;
    const auto input = data_path("invalid_lexical.tpp");
    const auto input_text = input.string();

    const bool succeeded = compiler.compile(input, session);

    TPP_CHECK(!succeeded);
    TPP_CHECK(session.diagnostics().has_errors());
    TPP_CHECK_EQ(session.diagnostics().error_count(), std::size_t{1});
    TPP_CHECK_EQ(session.tokens().size(), std::size_t{1});
    TPP_CHECK_EQ(session.tokens().front().kind, tpp::TokenKind::end_of_file);
    TPP_CHECK(!session.program().has_value());

    const auto diagnostics = session.diagnostics().diagnostics();
    TPP_CHECK_EQ(diagnostics.size(), std::size_t{1});
    TPP_CHECK_EQ(
        diagnostics.front().message,
        std::string{"unknown character '@'"});
    TPP_CHECK(diagnostics.front().primary_span.has_value());

    const auto span = *diagnostics.front().primary_span;
    TPP_CHECK_EQ(span.begin, std::size_t{0});
    TPP_CHECK_EQ(span.end, std::size_t{1});
    TPP_CHECK_EQ(
        session.sources().display_name(span.source),
        std::string_view{input_text});
    check_fresh_semantic_state(session);
}

void missing_source_fails_without_tokens()
{
    tpp::CompilationSession session;
    const tpp::Compiler compiler;
    const auto input = data_path("does-not-exist.tpp");

    const bool succeeded = compiler.compile(input, session);

    TPP_CHECK(!succeeded);
    TPP_CHECK(session.tokens().empty());
    TPP_CHECK(session.diagnostics().has_errors());
    TPP_CHECK_EQ(session.diagnostics().error_count(), std::size_t{1});

    const auto diagnostics = session.diagnostics().diagnostics();
    TPP_CHECK_EQ(diagnostics.size(), std::size_t{1});
    TPP_CHECK_EQ(
        diagnostics.front().message,
        std::string{"cannot open '"} + input.string() + '\'');
    TPP_CHECK(!diagnostics.front().primary_span.has_value());
    TPP_CHECK(!session.program().has_value());
    check_fresh_semantic_state(session);
}

void syntax_error_fails_after_storing_the_recovered_program()
{
    tpp::CompilationSession session;
    const tpp::Compiler compiler;

    const bool succeeded =
        compiler.compile(data_path("invalid_syntax.tpp"), session);

    TPP_CHECK(!succeeded);
    TPP_CHECK(session.program().has_value());
    TPP_CHECK(session.diagnostics().has_errors());
    TPP_CHECK_EQ(session.diagnostics().error_count(), std::size_t{1});
    TPP_CHECK_EQ(
        session.diagnostics().diagnostics().front().message,
        std::string{"expected expression"});
    check_fresh_semantic_state(session);
}

void compilation_session_can_be_reused_without_stale_results()
{
    tpp::CompilationSession session;
    const tpp::Compiler compiler;

    TPP_CHECK(!compiler.compile(
        data_path("invalid_lexical.tpp"),
        session));
    TPP_CHECK(session.diagnostics().has_errors());
    TPP_CHECK(!session.program().has_value());

    TPP_CHECK(!compiler.compile(
        data_path("duplicate_declaration.tpp"),
        session));
    TPP_CHECK(session.program().has_value());
    TPP_CHECK_EQ(session.diagnostics().error_count(), std::size_t{1});
    TPP_CHECK(!session.declarations().empty());
    TPP_CHECK(session.resolutions().empty());
    TPP_CHECK(session.type_info().empty());
    TPP_CHECK_EQ(session.symbols().symbol_count(), std::size_t{2});

    TPP_CHECK(!compiler.compile(
        data_path("unknown_name.tpp"),
        session));
    TPP_CHECK(session.program().has_value());
    TPP_CHECK_EQ(session.diagnostics().error_count(), std::size_t{1});
    TPP_CHECK(!session.declarations().empty());
    TPP_CHECK(!session.resolutions().empty());
    TPP_CHECK(session.type_info().empty());
    TPP_CHECK_EQ(session.symbols().symbol_count(), std::size_t{1});

    TPP_CHECK(!compiler.compile(
        data_path("type_error.tpp"),
        session));
    TPP_CHECK(session.program().has_value());
    TPP_CHECK_EQ(session.diagnostics().error_count(), std::size_t{1});
    TPP_CHECK(!session.declarations().empty());
    TPP_CHECK(session.resolutions().empty());
    TPP_CHECK(!session.type_info().empty());
    TPP_CHECK_EQ(session.symbols().symbol_count(), std::size_t{2});

    TPP_CHECK(!compiler.compile(
        data_path("control_flow_error.tpp"),
        session));
    TPP_CHECK(session.program().has_value());
    TPP_CHECK_EQ(session.diagnostics().error_count(), std::size_t{1});
    TPP_CHECK(!session.declarations().empty());
    TPP_CHECK(session.resolutions().empty());
    TPP_CHECK(!session.type_info().empty());
    TPP_CHECK_EQ(session.symbols().symbol_count(), std::size_t{2});

    TPP_CHECK(compiler.compile(
        data_path("unreachable_warning.tpp"),
        session));
    TPP_CHECK(session.program().has_value());
    TPP_CHECK(!session.diagnostics().has_errors());
    TPP_CHECK_EQ(session.diagnostics().diagnostics().size(), std::size_t{1});
    TPP_CHECK_EQ(
        session.diagnostics().diagnostics().front().severity,
        tpp::DiagnosticSeverity::warning);
    TPP_CHECK(!session.declarations().empty());
    TPP_CHECK(!session.resolutions().empty());
    TPP_CHECK(!session.type_info().empty());
    TPP_CHECK_EQ(session.symbols().symbol_count(), std::size_t{1});

    TPP_CHECK(compiler.compile(
        data_path("empty.tpp"),
        session));
    TPP_CHECK(session.program().has_value());
    TPP_CHECK(session.program()->declarations.empty());
    TPP_CHECK(!session.diagnostics().has_errors());
    check_fresh_semantic_state(session);

    TPP_CHECK(compiler.compile(
        data_path("valid_lexical.tpp"),
        session));
    TPP_CHECK(!session.diagnostics().has_errors());
    TPP_CHECK(session.diagnostics().diagnostics().empty());
    TPP_CHECK(session.program().has_value());
    TPP_CHECK_EQ(
        session.tokens().front().span.source.value,
        std::size_t{0});
    TPP_CHECK(!session.declarations().empty());
    TPP_CHECK(session.resolutions().empty());
    TPP_CHECK(!session.type_info().empty());
    TPP_CHECK_EQ(session.symbols().symbol_count(), std::size_t{1});
    TPP_CHECK_EQ(session.symbols().scope_count(), std::size_t{2});

    TPP_CHECK(!compiler.compile(
        data_path("does-not-exist.tpp"),
        session));
    TPP_CHECK(session.tokens().empty());
    TPP_CHECK(!session.program().has_value());
    TPP_CHECK_EQ(session.diagnostics().error_count(), std::size_t{1});
    check_fresh_semantic_state(session);
}

void compilation_session_reset_clears_semantic_state()
{
    tpp::CompilationSession session;
    const tpp::Compiler compiler;
    TPP_CHECK(compiler.compile(data_path("unreachable_warning.tpp"), session));
    TPP_CHECK(!session.declarations().empty());
    TPP_CHECK(!session.resolutions().empty());
    TPP_CHECK(!session.type_info().empty());
    TPP_CHECK_EQ(session.diagnostics().error_count(), std::size_t{0});
    TPP_CHECK_EQ(session.diagnostics().diagnostics().size(), std::size_t{1});
    TPP_CHECK_EQ(
        session.diagnostics().diagnostics().front().severity,
        tpp::DiagnosticSeverity::warning);

    const auto integer_type = session.types().integer_type();
    const auto vector_type = session.types().vector_type(integer_type);
    TPP_CHECK(vector_type.has_value());

    const auto child_scope = session.symbols().create_child_scope(
        session.symbols().global_scope());
    const auto insertion = session.symbols().insert(
        child_scope,
        tpp::Symbol{
            .name = "temporary",
            .declaration_span = tpp::SourceSpan{tpp::SourceId{0}, 1, 10},
            .data = tpp::VariableSymbol{integer_type},
        });

    TPP_CHECK(std::holds_alternative<tpp::SymbolId>(insertion));
    const auto symbol = std::get<tpp::SymbolId>(insertion);
    TPP_CHECK_EQ(session.types().type_count(), std::size_t{6});
    TPP_CHECK_EQ(session.symbols().scope_count(), std::size_t{3});
    TPP_CHECK_EQ(session.symbols().symbol_count(), std::size_t{2});

    session.reset();

    check_fresh_semantic_state(session);
    TPP_CHECK_EQ(session.types().integer_type(), integer_type);
    TPP_CHECK(!session.types().lookup(*vector_type).has_value());
    TPP_CHECK(!session.symbols()
                   .lookup_local(
                       session.symbols().global_scope(),
                       "temporary")
                   .has_value());
    require_out_of_range([&] {
        static_cast<void>(session.symbols().scope(child_scope));
    });
    require_out_of_range([&] {
        static_cast<void>(session.symbols().symbol(symbol));
    });
}

void compiler_populates_semantic_state_after_reset()
{
    tpp::CompilationSession session;
    const tpp::Compiler compiler;

    const auto child_scope = session.symbols().create_child_scope(
        session.symbols().global_scope());
    const auto insertion = session.symbols().insert(
        child_scope,
        tpp::Symbol{
            .name = "stale",
            .declaration_span = tpp::SourceSpan{tpp::SourceId{0}, 0, 5},
            .data = tpp::VariableSymbol{session.types().integer_type()},
        });
    TPP_CHECK(std::holds_alternative<tpp::SymbolId>(insertion));

    TPP_CHECK(compiler.compile(data_path("valid_lexical.tpp"), session));

    TPP_CHECK(session.program().has_value());
    TPP_CHECK_EQ(session.program()->declarations.size(), std::size_t{1});
    TPP_CHECK_EQ(session.types().type_count(), std::size_t{5});
    TPP_CHECK_EQ(session.symbols().scope_count(), std::size_t{2});
    TPP_CHECK_EQ(session.symbols().symbol_count(), std::size_t{1});
    TPP_CHECK(!session.declarations().empty());
    TPP_CHECK(session.resolutions().empty());
    TPP_CHECK(!session.type_info().empty());
    TPP_CHECK(!session.symbols()
                   .lookup_local(session.symbols().global_scope(), "stale")
                   .has_value());

    const auto& function = std::get<tpp::FunctionDeclaration>(
        session.program()->declarations.front());
    const auto main_symbol = session.declarations().symbol_for(function);
    const auto function_scope = session.declarations().scope_for(*function.body);
    TPP_CHECK(main_symbol.has_value());
    TPP_CHECK(function_scope.has_value());
    TPP_CHECK_EQ(
        session.symbols().lookup_local(
            session.symbols().global_scope(),
            "main"),
        main_symbol);
    TPP_CHECK_EQ(
        session.symbols().scope(*function_scope).parent(),
        std::optional<tpp::ScopeId>{session.symbols().global_scope()});
}

void declaration_errors_fail_after_collecting_independent_state()
{
    tpp::CompilationSession session;
    const tpp::Compiler compiler;

    const bool succeeded = compiler.compile(
        data_path("duplicate_declaration.tpp"),
        session);

    TPP_CHECK(!succeeded);
    TPP_CHECK(session.program().has_value());
    TPP_CHECK_EQ(session.diagnostics().error_count(), std::size_t{1});
    TPP_CHECK_EQ(session.diagnostics().diagnostics().size(), std::size_t{2});
    TPP_CHECK_EQ(session.symbols().scope_count(), std::size_t{2});
    TPP_CHECK_EQ(session.symbols().symbol_count(), std::size_t{2});
    TPP_CHECK(!session.declarations().empty());
    TPP_CHECK(session.resolutions().empty());
    TPP_CHECK(session.type_info().empty());

    const auto diagnostics = session.diagnostics().diagnostics();
    TPP_CHECK_EQ(
        diagnostics[0].severity,
        tpp::DiagnosticSeverity::error);
    TPP_CHECK_EQ(
        diagnostics[0].message,
        std::string{"duplicate declaration of 'value'"});
    TPP_CHECK(diagnostics[0].primary_span.has_value());
    TPP_CHECK_EQ(diagnostics[0].primary_span->begin, std::size_t{36});
    TPP_CHECK_EQ(diagnostics[0].primary_span->end, std::size_t{41});
    TPP_CHECK_EQ(
        diagnostics[1].severity,
        tpp::DiagnosticSeverity::note);
    TPP_CHECK_EQ(
        diagnostics[1].message,
        std::string{"previous declaration is here"});
    TPP_CHECK(diagnostics[1].primary_span.has_value());
    TPP_CHECK_EQ(diagnostics[1].primary_span->begin, std::size_t{21});
    TPP_CHECK_EQ(diagnostics[1].primary_span->end, std::size_t{26});

    const auto& function = std::get<tpp::FunctionDeclaration>(
        session.program()->declarations.front());
    TPP_CHECK(function.body != nullptr);
    TPP_CHECK_EQ(function.body->items.size(), std::size_t{2});

    const auto& first_statement =
        std::get<tpp::Statement>(function.body->items[0]);
    const auto& duplicate_statement =
        std::get<tpp::Statement>(function.body->items[1]);
    const auto& first =
        std::get<tpp::VariableDeclaration>(first_statement.node);
    const auto& duplicate =
        std::get<tpp::VariableDeclaration>(duplicate_statement.node);

    TPP_CHECK(session.declarations().symbol_for(function).has_value());
    TPP_CHECK(session.declarations().scope_for(*function.body).has_value());
    TPP_CHECK(session.declarations().symbol_for(first).has_value());
    TPP_CHECK(!session.declarations().symbol_for(duplicate).has_value());
}

void compiler_resolves_user_names_and_builtins()
{
    tpp::CompilationSession session;
    const tpp::Compiler compiler;

    TPP_CHECK(compiler.compile(
        data_path("name_resolution_valid.tpp"),
        session));
    TPP_CHECK(!session.diagnostics().has_errors());
    TPP_CHECK(session.program().has_value());
    TPP_CHECK(!session.resolutions().empty());
    TPP_CHECK(!session.type_info().empty());

    const auto& declarations = session.program()->declarations;
    TPP_CHECK_EQ(declarations.size(), std::size_t{3});

    const auto& global = std::get<tpp::VariableDeclaration>(declarations[0]);
    const auto& identity = std::get<tpp::FunctionDeclaration>(declarations[1]);
    const auto& main = std::get<tpp::FunctionDeclaration>(declarations[2]);
    TPP_CHECK(identity.body != nullptr);
    TPP_CHECK(main.body != nullptr);
    TPP_CHECK_EQ(identity.parameters.size(), std::size_t{1});
    TPP_CHECK_EQ(identity.body->items.size(), std::size_t{1});
    TPP_CHECK_EQ(main.body->items.size(), std::size_t{4});

    const auto global_symbol = session.declarations().symbol_for(global);
    const auto identity_symbol = session.declarations().symbol_for(identity);
    const auto parameter_symbol =
        session.declarations().symbol_for(identity.parameters.front());
    TPP_CHECK(global_symbol.has_value());
    TPP_CHECK(identity_symbol.has_value());
    TPP_CHECK(parameter_symbol.has_value());

    const auto& identity_return_statement = std::get<tpp::Statement>(
        identity.body->items.front());
    const auto& identity_return = std::get<tpp::ReturnStatement>(
        identity_return_statement.node);
    TPP_CHECK(identity_return.value != nullptr);
    const auto& parameter_reference = std::get<tpp::IdentifierExpression>(
        identity_return.value->node);
    check_resolution(
        session.resolutions(),
        parameter_reference,
        tpp::ResolutionTarget{*parameter_symbol});

    const auto& local_statement =
        std::get<tpp::Statement>(main.body->items[0]);
    const auto& local =
        std::get<tpp::VariableDeclaration>(local_statement.node);
    const auto local_symbol = session.declarations().symbol_for(local);
    TPP_CHECK(local_symbol.has_value());
    TPP_CHECK(local.initializer != nullptr);
    const auto& global_reference = std::get<tpp::IdentifierExpression>(
        local.initializer->node);
    check_resolution(
        session.resolutions(),
        global_reference,
        tpp::ResolutionTarget{*global_symbol});

    const auto& assignment_statement =
        std::get<tpp::Statement>(main.body->items[1]);
    const auto& assignment =
        std::get<tpp::AssignmentStatement>(assignment_statement.node);
    const auto assignment_target =
        session.resolutions().resolution_for(assignment.target);
    TPP_CHECK(assignment_target.has_value());
    TPP_CHECK_EQ(
        *assignment_target,
        tpp::ResolutionTarget{*local_symbol});
    TPP_CHECK(assignment.value != nullptr);

    const auto& identity_call =
        std::get<tpp::CallExpression>(assignment.value->node);
    TPP_CHECK(identity_call.callee != nullptr);
    TPP_CHECK_EQ(identity_call.arguments.size(), std::size_t{1});
    TPP_CHECK(identity_call.arguments.front() != nullptr);
    const auto& identity_reference = std::get<tpp::IdentifierExpression>(
        identity_call.callee->node);
    const auto& local_argument = std::get<tpp::IdentifierExpression>(
        identity_call.arguments.front()->node);
    check_resolution(
        session.resolutions(),
        identity_reference,
        tpp::ResolutionTarget{*identity_symbol});
    check_resolution(
        session.resolutions(),
        local_argument,
        tpp::ResolutionTarget{*local_symbol});

    const auto& print_statement =
        std::get<tpp::Statement>(main.body->items[2]);
    const auto& print_expression =
        std::get<tpp::ExpressionStatement>(print_statement.node);
    TPP_CHECK(print_expression.expression != nullptr);
    const auto& print_call =
        std::get<tpp::CallExpression>(print_expression.expression->node);
    TPP_CHECK(print_call.callee != nullptr);
    TPP_CHECK_EQ(print_call.arguments.size(), std::size_t{1});
    TPP_CHECK(print_call.arguments.front() != nullptr);
    const auto& print_reference = std::get<tpp::IdentifierExpression>(
        print_call.callee->node);
    const auto& printed_local = std::get<tpp::IdentifierExpression>(
        print_call.arguments.front()->node);
    check_resolution(
        session.resolutions(),
        print_reference,
        tpp::ResolutionTarget{tpp::BuiltinFunctionKind::print});
    check_resolution(
        session.resolutions(),
        printed_local,
        tpp::ResolutionTarget{*local_symbol});
}

void name_resolution_errors_retain_partial_semantic_state()
{
    tpp::CompilationSession session;
    const tpp::Compiler compiler;

    const bool succeeded = compiler.compile(
        data_path("unknown_name.tpp"),
        session);

    TPP_CHECK(!succeeded);
    TPP_CHECK(session.program().has_value());
    TPP_CHECK(!session.declarations().empty());
    TPP_CHECK(!session.resolutions().empty());
    TPP_CHECK(session.type_info().empty());
    TPP_CHECK_EQ(session.symbols().symbol_count(), std::size_t{1});
    TPP_CHECK_EQ(session.symbols().scope_count(), std::size_t{2});
    TPP_CHECK_EQ(session.diagnostics().error_count(), std::size_t{1});
    TPP_CHECK_EQ(session.diagnostics().diagnostics().size(), std::size_t{1});

    const auto& diagnostic = session.diagnostics().diagnostics().front();
    TPP_CHECK_EQ(diagnostic.severity, tpp::DiagnosticSeverity::error);
    TPP_CHECK_EQ(diagnostic.message, std::string{"unknown name 'missing'"});
    TPP_CHECK(diagnostic.primary_span.has_value());
    TPP_CHECK_EQ(diagnostic.primary_span->begin, std::size_t{23});
    TPP_CHECK_EQ(diagnostic.primary_span->end, std::size_t{30});

    const auto& main = std::get<tpp::FunctionDeclaration>(
        session.program()->declarations.front());
    TPP_CHECK(main.body != nullptr);
    const auto& statement = std::get<tpp::Statement>(main.body->items.front());
    const auto& expression_statement =
        std::get<tpp::ExpressionStatement>(statement.node);
    TPP_CHECK(expression_statement.expression != nullptr);
    const auto& call = std::get<tpp::CallExpression>(
        expression_statement.expression->node);
    TPP_CHECK(call.callee != nullptr);
    TPP_CHECK_EQ(call.arguments.size(), std::size_t{1});
    TPP_CHECK(call.arguments.front() != nullptr);

    const auto& print_reference = std::get<tpp::IdentifierExpression>(
        call.callee->node);
    const auto& missing_reference = std::get<tpp::IdentifierExpression>(
        call.arguments.front()->node);
    check_resolution(
        session.resolutions(),
        print_reference,
        tpp::ResolutionTarget{tpp::BuiltinFunctionKind::print});
    TPP_CHECK(!session.resolutions()
                   .resolution_for(missing_reference)
                   .has_value());
}

void type_errors_retain_partial_type_information()
{
    tpp::CompilationSession session;
    const tpp::Compiler compiler;

    const bool succeeded = compiler.compile(
        data_path("type_error.tpp"),
        session);

    TPP_CHECK(!succeeded);
    TPP_CHECK(session.program().has_value());
    TPP_CHECK(!session.declarations().empty());
    TPP_CHECK(session.resolutions().empty());
    TPP_CHECK(!session.type_info().empty());
    TPP_CHECK_EQ(session.symbols().symbol_count(), std::size_t{2});
    TPP_CHECK_EQ(session.symbols().scope_count(), std::size_t{2});
    TPP_CHECK_EQ(session.diagnostics().error_count(), std::size_t{1});
    TPP_CHECK_EQ(session.diagnostics().diagnostics().size(), std::size_t{1});

    const auto& diagnostic = session.diagnostics().diagnostics().front();
    TPP_CHECK_EQ(diagnostic.severity, tpp::DiagnosticSeverity::error);
    TPP_CHECK_EQ(
        diagnostic.message,
        std::string{
            "cannot initialize 'int' with value of type 'string'"});
    TPP_CHECK(diagnostic.primary_span.has_value());
    TPP_CHECK_EQ(diagnostic.primary_span->begin, std::size_t{29});
    TPP_CHECK_EQ(diagnostic.primary_span->end, std::size_t{35});

    const auto& main = std::get<tpp::FunctionDeclaration>(
        session.program()->declarations.front());
    TPP_CHECK(main.body != nullptr);
    TPP_CHECK_EQ(main.body->items.size(), std::size_t{1});
    const auto& statement =
        std::get<tpp::Statement>(main.body->items.front());
    const auto& variable =
        std::get<tpp::VariableDeclaration>(statement.node);
    TPP_CHECK(variable.initializer != nullptr);
    TPP_CHECK_EQ(
        session.type_info().type_of(*variable.initializer),
        std::optional<tpp::TypeId>{session.types().string_type()});
}

void control_flow_errors_fail_after_type_checking()
{
    tpp::CompilationSession session;
    const tpp::Compiler compiler;

    const bool succeeded = compiler.compile(
        data_path("control_flow_error.tpp"),
        session);

    TPP_CHECK(!succeeded);
    TPP_CHECK(session.program().has_value());
    TPP_CHECK(!session.declarations().empty());
    TPP_CHECK(session.resolutions().empty());
    TPP_CHECK(!session.type_info().empty());
    TPP_CHECK_EQ(session.symbols().symbol_count(), std::size_t{2});
    TPP_CHECK_EQ(session.symbols().scope_count(), std::size_t{3});
    TPP_CHECK_EQ(session.diagnostics().error_count(), std::size_t{1});
    TPP_CHECK_EQ(session.diagnostics().diagnostics().size(), std::size_t{1});

    const auto& diagnostic = session.diagnostics().diagnostics().front();
    TPP_CHECK_EQ(diagnostic.severity, tpp::DiagnosticSeverity::error);
    TPP_CHECK_EQ(
        diagnostic.message,
        std::string{"'break' is only allowed inside a loop"});
    TPP_CHECK(diagnostic.primary_span.has_value());
    TPP_CHECK_EQ(diagnostic.primary_span->begin, std::size_t{20});
    TPP_CHECK_EQ(diagnostic.primary_span->end, std::size_t{26});

    const auto& main = std::get<tpp::FunctionDeclaration>(
        session.program()->declarations[1]);
    TPP_CHECK(main.body != nullptr);
    TPP_CHECK_EQ(main.body->items.size(), std::size_t{1});
    const auto& statement =
        std::get<tpp::Statement>(main.body->items.front());
    const auto& return_statement =
        std::get<tpp::ReturnStatement>(statement.node);
    TPP_CHECK(return_statement.value != nullptr);
    TPP_CHECK_EQ(
        session.type_info().type_of(*return_statement.value),
        std::optional<tpp::TypeId>{session.types().integer_type()});
}

void unreachable_warnings_do_not_fail_compilation()
{
    tpp::CompilationSession session;
    const tpp::Compiler compiler;

    const bool succeeded = compiler.compile(
        data_path("unreachable_warning.tpp"),
        session);

    TPP_CHECK(succeeded);
    TPP_CHECK(session.program().has_value());
    TPP_CHECK(!session.declarations().empty());
    TPP_CHECK(!session.resolutions().empty());
    TPP_CHECK(!session.type_info().empty());
    TPP_CHECK_EQ(session.symbols().symbol_count(), std::size_t{1});
    TPP_CHECK_EQ(session.symbols().scope_count(), std::size_t{2});
    TPP_CHECK(!session.diagnostics().has_errors());
    TPP_CHECK_EQ(session.diagnostics().error_count(), std::size_t{0});
    TPP_CHECK_EQ(session.diagnostics().diagnostics().size(), std::size_t{1});

    const auto& diagnostic = session.diagnostics().diagnostics().front();
    TPP_CHECK_EQ(diagnostic.severity, tpp::DiagnosticSeverity::warning);
    TPP_CHECK_EQ(diagnostic.message, std::string{"unreachable statement"});
    TPP_CHECK(diagnostic.primary_span.has_value());
    TPP_CHECK_EQ(diagnostic.primary_span->begin, std::size_t{31});
    TPP_CHECK_EQ(diagnostic.primary_span->end, std::size_t{40});
}

}

int main()
{
    return tpp::test::run({
        {"empty source produces only EOF", empty_source_produces_only_eof},
        {"valid source produces tokens without diagnostics",
         valid_source_produces_tokens_without_diagnostics},
        {"lexical error fails but recovers to EOF",
         lexical_error_fails_but_recovers_to_eof},
        {"missing source fails without tokens",
         missing_source_fails_without_tokens},
        {"syntax error stores recovered program",
         syntax_error_fails_after_storing_the_recovered_program},
        {"compilation session reuse resets results",
         compilation_session_can_be_reused_without_stale_results},
        {"compilation session reset clears semantic state",
         compilation_session_reset_clears_semantic_state},
        {"compiler populates semantic state after reset",
         compiler_populates_semantic_state_after_reset},
        {"declaration errors retain collected state",
         declaration_errors_fail_after_collecting_independent_state},
        {"compiler resolves user names and builtins",
         compiler_resolves_user_names_and_builtins},
        {"name errors retain partial semantic state",
         name_resolution_errors_retain_partial_semantic_state},
        {"type errors retain partial type information",
         type_errors_retain_partial_type_information},
        {"control-flow errors follow successful type checking",
         control_flow_errors_fail_after_type_checking},
        {"unreachable warnings preserve compilation success",
         unreachable_warnings_do_not_fail_compilation},
    });
}

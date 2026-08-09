#include "test_support.hpp"

#include "pseudo/ast/program.hpp"
#include "pseudo/driver/compilation_session.hpp"
#include "pseudo/driver/compiler.hpp"
#include "pseudo/lexer/token.hpp"

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

    TPP_CHECK(compiler.compile(
        data_path("valid_lexical.tpp"),
        session));
    TPP_CHECK(!session.diagnostics().has_errors());
    TPP_CHECK(session.diagnostics().diagnostics().empty());
    TPP_CHECK(session.program().has_value());
    TPP_CHECK_EQ(
        session.tokens().front().span.source.value,
        std::size_t{0});

    TPP_CHECK(!compiler.compile(
        data_path("does-not-exist.tpp"),
        session));
    TPP_CHECK(session.tokens().empty());
    TPP_CHECK(!session.program().has_value());
    TPP_CHECK_EQ(session.diagnostics().error_count(), std::size_t{1});
}

void compilation_session_reset_clears_semantic_state()
{
    tpp::CompilationSession session;
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
    TPP_CHECK_EQ(session.symbols().scope_count(), std::size_t{2});
    TPP_CHECK_EQ(session.symbols().symbol_count(), std::size_t{1});

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

void compiler_resets_but_does_not_populate_semantic_state()
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

    check_fresh_semantic_state(session);
    TPP_CHECK(session.program().has_value());
    TPP_CHECK_EQ(session.program()->declarations.size(), std::size_t{1});
    TPP_CHECK(!session.symbols()
                   .lookup_local(session.symbols().global_scope(), "main")
                   .has_value());
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
        {"compiler does not populate semantic state",
         compiler_resets_but_does_not_populate_semantic_state},
    });
}

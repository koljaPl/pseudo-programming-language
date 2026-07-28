#include "test_support.hpp"

#include "pseudo/driver/compilation_session.hpp"
#include "pseudo/driver/compiler.hpp"
#include "pseudo/lexer/token.hpp"

#include <filesystem>
#include <string>

#ifndef TPP_TEST_DATA_DIR
#error "TPP_TEST_DATA_DIR must point to the tests/data directory"
#endif

namespace {

std::filesystem::path data_path(const std::string& filename)
{
    return std::filesystem::path{TPP_TEST_DATA_DIR} / filename;
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
    });
}

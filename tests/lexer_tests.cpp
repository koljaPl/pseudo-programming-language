#include "test_support.hpp"

#include "pseudo/diagnostics/diagnostic_engine.hpp"
#include "pseudo/lexer/lexer.hpp"
#include "pseudo/lexer/token.hpp"
#include "pseudo/source/source_manager.hpp"

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

using tpp::DiagnosticEngine;
using tpp::Lexer;
using tpp::SourceId;
using tpp::SourceManager;
using tpp::Token;
using tpp::TokenKind;

class LexingResult {
public:
    explicit LexingResult(
        std::string contents,
        std::string display_name = "lexer.tpp")
        : source{sources.add_source(
              std::move(display_name),
              std::move(contents))}
    {
        Lexer lexer{source, sources, diagnostics};
        tokens = lexer.lex();
    }

    SourceManager sources;
    DiagnosticEngine diagnostics;
    SourceId source;
    std::vector<Token> tokens;
};

struct ExpectedToken {
    TokenKind kind;
    std::string_view spelling;
};

void check_span(
    const Token& token,
    SourceId source,
    const std::size_t begin,
    const std::size_t end)
{
    TPP_CHECK(token.span.source == source);
    TPP_CHECK_EQ(token.span.begin, begin);
    TPP_CHECK_EQ(token.span.end, end);
}

void check_eof(const LexingResult& result)
{
    const auto eof_count = std::count_if(
        result.tokens.begin(),
        result.tokens.end(),
        [](const Token& token) {
            return token.kind == TokenKind::end_of_file;
        });

    TPP_CHECK_EQ(eof_count, std::ptrdiff_t{1});
    TPP_CHECK(!result.tokens.empty());

    const auto& eof = result.tokens.back();
    TPP_CHECK_EQ(eof.kind, TokenKind::end_of_file);
    const auto source_size = result.sources.contents(result.source).size();
    check_span(eof, result.source, source_size, source_size);
    TPP_CHECK(std::holds_alternative<std::monostate>(eof.value));
}

void check_kinds(
    const LexingResult& result,
    const std::initializer_list<TokenKind> expected)
{
    TPP_CHECK_EQ(result.tokens.size(), expected.size() + std::size_t{1});

    std::size_t index = 0;
    for (const auto kind : expected) {
        TPP_CHECK_EQ(result.tokens[index].kind, kind);
        ++index;
    }

    check_eof(result);
}

void check_spelled_tokens(
    const LexingResult& result,
    const std::initializer_list<ExpectedToken> expected)
{
    TPP_CHECK_EQ(result.tokens.size(), expected.size() + std::size_t{1});

    const auto contents = result.sources.contents(result.source);
    std::size_t cursor = 0;
    std::size_t index = 0;

    for (const auto& expected_token : expected) {
        const auto begin = contents.find(expected_token.spelling, cursor);
        TPP_CHECK(begin != std::string_view::npos);
        const auto end = begin + expected_token.spelling.size();

        const auto& actual = result.tokens[index];
        TPP_CHECK_EQ(actual.kind, expected_token.kind);
        check_span(actual, result.source, begin, end);
        TPP_CHECK_EQ(result.sources.slice(actual.span), expected_token.spelling);
        TPP_CHECK(std::holds_alternative<std::monostate>(actual.value));

        cursor = end;
        ++index;
    }

    check_eof(result);
}

void check_single_error(
    const LexingResult& result,
    const std::string_view message,
    const std::size_t begin,
    const std::size_t end)
{
    TPP_CHECK(result.diagnostics.has_errors());
    TPP_CHECK_EQ(result.diagnostics.error_count(), std::size_t{1});

    const auto diagnostics = result.diagnostics.diagnostics();
    TPP_CHECK_EQ(diagnostics.size(), std::size_t{1});
    TPP_CHECK_EQ(diagnostics.front().message, message);
    TPP_CHECK(diagnostics.front().primary_span.has_value());

    const auto span = *diagnostics.front().primary_span;
    TPP_CHECK(span.source == result.source);
    TPP_CHECK_EQ(span.begin, begin);
    TPP_CHECK_EQ(span.end, end);
}

std::string render(const LexingResult& result)
{
    std::ostringstream output;
    tpp::render_diagnostics(
        output,
        result.diagnostics.diagnostics(),
        result.sources);
    return output.str();
}

void empty_source_produces_exactly_one_eof()
{
    const LexingResult result{""};

    TPP_CHECK(!result.diagnostics.has_errors());
    TPP_CHECK(result.diagnostics.diagnostics().empty());
    TPP_CHECK_EQ(result.tokens.size(), std::size_t{1});
    check_eof(result);
}

void identifiers_are_ascii_and_keep_source_spelling()
{
    const LexingResult result{
        "_ _name name1 A Zz9 intValue trueValue And and_"};

    check_spelled_tokens(
        result,
        {
            {TokenKind::identifier, "_"},
            {TokenKind::identifier, "_name"},
            {TokenKind::identifier, "name1"},
            {TokenKind::identifier, "A"},
            {TokenKind::identifier, "Zz9"},
            {TokenKind::identifier, "intValue"},
            {TokenKind::identifier, "trueValue"},
            {TokenKind::identifier, "And"},
            {TokenKind::identifier, "and_"},
        });
    TPP_CHECK(!result.diagnostics.has_errors());
}

void all_reserved_words_are_classified()
{
    const LexingResult result{
        "int bool char string void vector if else while for in return "
        "break continue and or not"};

    check_spelled_tokens(
        result,
        {
            {TokenKind::keyword_int, "int"},
            {TokenKind::keyword_bool, "bool"},
            {TokenKind::keyword_char, "char"},
            {TokenKind::keyword_string, "string"},
            {TokenKind::keyword_void, "void"},
            {TokenKind::keyword_vector, "vector"},
            {TokenKind::keyword_if, "if"},
            {TokenKind::keyword_else, "else"},
            {TokenKind::keyword_while, "while"},
            {TokenKind::keyword_for, "for"},
            {TokenKind::keyword_in, "in"},
            {TokenKind::keyword_return, "return"},
            {TokenKind::keyword_break, "break"},
            {TokenKind::keyword_continue, "continue"},
            {TokenKind::logical_and, "and"},
            {TokenKind::logical_or, "or"},
            {TokenKind::logical_not, "not"},
        });
    TPP_CHECK(!result.diagnostics.has_errors());
}

void boolean_literals_carry_boolean_values()
{
    const LexingResult result{"true false"};

    check_kinds(
        result,
        {
            TokenKind::boolean_literal,
            TokenKind::boolean_literal,
        });
    check_span(result.tokens[0], result.source, 0, 4);
    check_span(result.tokens[1], result.source, 5, 10);
    TPP_CHECK_EQ(std::get<bool>(result.tokens[0].value), true);
    TPP_CHECK_EQ(std::get<bool>(result.tokens[1].value), false);
    TPP_CHECK(!result.diagnostics.has_errors());
}

void integer_literals_remain_unconverted_source_slices()
{
    const LexingResult result{
        "0 007 184467440737095516161 -42 123abc"};

    check_spelled_tokens(
        result,
        {
            {TokenKind::integer_literal, "0"},
            {TokenKind::integer_literal, "007"},
            {TokenKind::integer_literal, "184467440737095516161"},
            {TokenKind::minus, "-"},
            {TokenKind::integer_literal, "42"},
            {TokenKind::integer_literal, "123"},
            {TokenKind::identifier, "abc"},
        });
    TPP_CHECK(!result.diagnostics.has_errors());
}

void all_punctuation_is_tokenized()
{
    const LexingResult result{"( ) { } [ ] , ; ."};

    check_spelled_tokens(
        result,
        {
            {TokenKind::left_parenthesis, "("},
            {TokenKind::right_parenthesis, ")"},
            {TokenKind::left_brace, "{"},
            {TokenKind::right_brace, "}"},
            {TokenKind::left_bracket, "["},
            {TokenKind::right_bracket, "]"},
            {TokenKind::comma, ","},
            {TokenKind::semicolon, ";"},
            {TokenKind::dot, "."},
        });
    TPP_CHECK(!result.diagnostics.has_errors());
}

void all_symbolic_operators_are_tokenized()
{
    const LexingResult result{
        "= + - * / % += -= *= /= %= == != < <= > >= && || ! .. ..="};

    check_spelled_tokens(
        result,
        {
            {TokenKind::assign, "="},
            {TokenKind::plus, "+"},
            {TokenKind::minus, "-"},
            {TokenKind::star, "*"},
            {TokenKind::slash, "/"},
            {TokenKind::percent, "%"},
            {TokenKind::plus_assign, "+="},
            {TokenKind::minus_assign, "-="},
            {TokenKind::star_assign, "*="},
            {TokenKind::slash_assign, "/="},
            {TokenKind::percent_assign, "%="},
            {TokenKind::equal, "=="},
            {TokenKind::not_equal, "!="},
            {TokenKind::less, "<"},
            {TokenKind::less_equal, "<="},
            {TokenKind::greater, ">"},
            {TokenKind::greater_equal, ">="},
            {TokenKind::logical_and, "&&"},
            {TokenKind::logical_or, "||"},
            {TokenKind::logical_not, "!"},
            {TokenKind::range_exclusive, ".."},
            {TokenKind::range_inclusive, "..="},
        });
    TPP_CHECK(!result.diagnostics.has_errors());
}

void operators_use_longest_match()
{
    const LexingResult result{
        "..=.. !== <=< >=> == = +=+ -=- *=* /=/ %=%"};

    check_spelled_tokens(
        result,
        {
            {TokenKind::range_inclusive, "..="},
            {TokenKind::range_exclusive, ".."},
            {TokenKind::not_equal, "!="},
            {TokenKind::assign, "="},
            {TokenKind::less_equal, "<="},
            {TokenKind::less, "<"},
            {TokenKind::greater_equal, ">="},
            {TokenKind::greater, ">"},
            {TokenKind::equal, "=="},
            {TokenKind::assign, "="},
            {TokenKind::plus_assign, "+="},
            {TokenKind::plus, "+"},
            {TokenKind::minus_assign, "-="},
            {TokenKind::minus, "-"},
            {TokenKind::star_assign, "*="},
            {TokenKind::star, "*"},
            {TokenKind::slash_assign, "/="},
            {TokenKind::slash, "/"},
            {TokenKind::percent_assign, "%="},
            {TokenKind::percent, "%"},
        });
    TPP_CHECK(!result.diagnostics.has_errors());
}

void whitespace_and_line_comments_are_ignored()
{
    const LexingResult result{
        "\t alpha // ignored != ..=\r\n beta// comment at EOF"};

    check_spelled_tokens(
        result,
        {
            {TokenKind::identifier, "alpha"},
            {TokenKind::identifier, "beta"},
        });
    TPP_CHECK(!result.diagnostics.has_errors());
}

void simple_string_literals_carry_owned_values()
{
    const LexingResult result{R"pseudo("" "abc" "it's fine")pseudo"};

    check_kinds(
        result,
        {
            TokenKind::string_literal,
            TokenKind::string_literal,
            TokenKind::string_literal,
        });

    TPP_CHECK_EQ(std::get<std::string>(result.tokens[0].value), std::string{});
    TPP_CHECK_EQ(
        std::get<std::string>(result.tokens[1].value),
        std::string{"abc"});
    TPP_CHECK_EQ(
        std::get<std::string>(result.tokens[2].value),
        std::string{"it's fine"});

    check_span(result.tokens[0], result.source, 0, 2);
    check_span(result.tokens[1], result.source, 3, 8);
    check_span(result.tokens[2], result.source, 9, 20);
    TPP_CHECK(!result.diagnostics.has_errors());
}

void string_literal_decodes_every_supported_escape()
{
    const LexingResult result{
        R"pseudo("\\\"\'\n\r\t\0")pseudo"};

    check_kinds(result, {TokenKind::string_literal});

    std::string expected;
    expected.push_back('\\');
    expected.push_back('"');
    expected.push_back('\'');
    expected.push_back('\n');
    expected.push_back('\r');
    expected.push_back('\t');
    expected.push_back('\0');

    const auto& decoded = std::get<std::string>(result.tokens.front().value);
    TPP_CHECK_EQ(decoded, expected);
    TPP_CHECK_EQ(decoded.size(), std::size_t{7});
    check_span(
        result.tokens.front(),
        result.source,
        0,
        result.sources.contents(result.source).size());
    TPP_CHECK(!result.diagnostics.has_errors());
}

void strings_preserve_raw_bytes_and_embedded_nul()
{
    std::string source{"\"A"};
    source.push_back('\0');
    source.push_back(static_cast<char>(0xff));
    source += "Z\"";

    Token token = [&source] {
        SourceManager sources;
        DiagnosticEngine diagnostics;
        const auto source_id = sources.add_source("bytes.tpp", source);
        Lexer lexer{source_id, sources, diagnostics};
        auto tokens = lexer.lex();

        TPP_CHECK(!diagnostics.has_errors());
        TPP_CHECK_EQ(tokens.size(), std::size_t{2});
        TPP_CHECK_EQ(tokens.front().kind, TokenKind::string_literal);
        return std::move(tokens.front());
    }();

    std::string expected{"A"};
    expected.push_back('\0');
    expected.push_back(static_cast<char>(0xff));
    expected.push_back('Z');

    const auto& decoded = std::get<std::string>(token.value);
    TPP_CHECK_EQ(decoded, expected);
    TPP_CHECK_EQ(decoded.size(), std::size_t{4});
}

void character_literals_decode_every_supported_escape()
{
    const LexingResult result{
        R"pseudo('a' '\\' '\"' '\'' '\n' '\r' '\t' '\0')pseudo"};

    check_kinds(
        result,
        {
            TokenKind::character_literal,
            TokenKind::character_literal,
            TokenKind::character_literal,
            TokenKind::character_literal,
            TokenKind::character_literal,
            TokenKind::character_literal,
            TokenKind::character_literal,
            TokenKind::character_literal,
        });

    const std::vector<char> expected{
        'a',
        '\\',
        '"',
        '\'',
        '\n',
        '\r',
        '\t',
        '\0',
    };

    for (std::size_t index = 0; index < expected.size(); ++index) {
        TPP_CHECK_EQ(
            std::get<char>(result.tokens[index].value),
            expected[index]);
    }

    TPP_CHECK(!result.diagnostics.has_errors());
}

void raw_nul_is_a_valid_one_byte_character_literal()
{
    std::string source{"'"};
    source.push_back('\0');
    source.push_back('\'');
    const LexingResult result{std::move(source)};

    check_kinds(result, {TokenKind::character_literal});
    check_span(result.tokens.front(), result.source, 0, 3);
    TPP_CHECK_EQ(std::get<char>(result.tokens.front().value), '\0');
    TPP_CHECK(!result.diagnostics.has_errors());
}

void unknown_source_characters_report_exact_errors_and_make_progress()
{
    std::string source;
    source.push_back('\0');
    source.push_back('@');
    source.push_back(static_cast<char>(0xff));
    source.push_back('x');
    const LexingResult result{std::move(source)};

    check_kinds(result, {TokenKind::identifier});
    check_span(result.tokens.front(), result.source, 3, 4);
    TPP_CHECK_EQ(
        result.sources.slice(result.tokens.front().span),
        std::string_view{"x"});
    TPP_CHECK_EQ(result.diagnostics.error_count(), std::size_t{3});

    const auto diagnostics = result.diagnostics.diagnostics();
    TPP_CHECK_EQ(diagnostics.size(), std::size_t{3});

    const std::vector<std::string> messages{
        "unknown byte 0x00",
        "unknown character '@'",
        "unknown byte 0xFF",
    };

    for (std::size_t index = 0; index < diagnostics.size(); ++index) {
        TPP_CHECK_EQ(diagnostics[index].message, messages[index]);
        TPP_CHECK(diagnostics[index].primary_span.has_value());
        const auto span = *diagnostics[index].primary_span;
        TPP_CHECK(span.source == result.source);
        TPP_CHECK_EQ(span.begin, index);
        TPP_CHECK_EQ(span.end, index + 1);
    }
}

void independent_lexical_errors_preserve_tokens_and_source_order()
{
    const LexingResult result{
        R"pseudo(@ good & "\q" okay 'ab' tail |)pseudo"};

    check_spelled_tokens(
        result,
        {
            {TokenKind::identifier, "good"},
            {TokenKind::identifier, "okay"},
            {TokenKind::identifier, "tail"},
        });

    const auto diagnostics = result.diagnostics.diagnostics();
    TPP_CHECK_EQ(result.diagnostics.error_count(), std::size_t{5});
    TPP_CHECK_EQ(diagnostics.size(), std::size_t{5});

    const std::array expected_messages{
        std::string_view{"unknown character '@'"},
        std::string_view{"unexpected '&'; use '&&' for logical and"},
        std::string_view{"unknown escape sequence '\\q'"},
        std::string_view{"character literal must contain exactly one byte"},
        std::string_view{"unexpected '|'; use '||' for logical or"},
    };
    const std::array expected_spans{
        std::pair{std::size_t{0}, std::size_t{1}},
        std::pair{std::size_t{7}, std::size_t{8}},
        std::pair{std::size_t{10}, std::size_t{12}},
        std::pair{std::size_t{19}, std::size_t{23}},
        std::pair{std::size_t{29}, std::size_t{30}},
    };

    for (std::size_t index = 0; index < diagnostics.size(); ++index) {
        TPP_CHECK_EQ(diagnostics[index].message, expected_messages[index]);
        TPP_CHECK(diagnostics[index].primary_span.has_value());
        const auto span = *diagnostics[index].primary_span;
        TPP_CHECK(span.source == result.source);
        TPP_CHECK_EQ(span.begin, expected_spans[index].first);
        TPP_CHECK_EQ(span.end, expected_spans[index].second);
    }
}

void lexer_instances_can_be_reused_deterministically()
{
    SourceManager sources;
    DiagnosticEngine diagnostics;
    const auto source = sources.add_source(
        "reuse.tpp",
        R"pseudo(name "value" '\n')pseudo");
    Lexer lexer{source, sources, diagnostics};

    const auto first = lexer.lex();
    const auto second = lexer.lex();

    TPP_CHECK(!diagnostics.has_errors());
    TPP_CHECK_EQ(first.size(), second.size());
    for (std::size_t index = 0; index < first.size(); ++index) {
        TPP_CHECK_EQ(first[index].kind, second[index].kind);
        TPP_CHECK(first[index].span.source == second[index].span.source);
        TPP_CHECK_EQ(first[index].span.begin, second[index].span.begin);
        TPP_CHECK_EQ(first[index].span.end, second[index].span.end);
        TPP_CHECK(first[index].value == second[index].value);
    }
}

void solitary_ampersand_and_pipe_have_actionable_diagnostics()
{
    const LexingResult result{"& | && ||"};

    check_spelled_tokens(
        result,
        {
            {TokenKind::logical_and, "&&"},
            {TokenKind::logical_or, "||"},
        });

    const auto diagnostics = result.diagnostics.diagnostics();
    TPP_CHECK_EQ(result.diagnostics.error_count(), std::size_t{2});
    TPP_CHECK_EQ(diagnostics.size(), std::size_t{2});
    TPP_CHECK_EQ(
        diagnostics[0].message,
        std::string{"unexpected '&'; use '&&' for logical and"});
    TPP_CHECK_EQ(
        diagnostics[1].message,
        std::string{"unexpected '|'; use '||' for logical or"});

    TPP_CHECK(diagnostics[0].primary_span.has_value());
    TPP_CHECK(diagnostics[1].primary_span.has_value());
    TPP_CHECK_EQ(diagnostics[0].primary_span->begin, std::size_t{0});
    TPP_CHECK_EQ(diagnostics[0].primary_span->end, std::size_t{1});
    TPP_CHECK_EQ(diagnostics[1].primary_span->begin, std::size_t{2});
    TPP_CHECK_EQ(diagnostics[1].primary_span->end, std::size_t{3});
}

void unknown_escapes_discard_literals_and_recover_after_closing_quote()
{
    const LexingResult result{R"pseudo("\q" '\q' okay)pseudo"};

    check_spelled_tokens(
        result,
        {
            {TokenKind::identifier, "okay"},
        });

    const auto diagnostics = result.diagnostics.diagnostics();
    TPP_CHECK_EQ(result.diagnostics.error_count(), std::size_t{2});
    TPP_CHECK_EQ(diagnostics.size(), std::size_t{2});

    for (const auto& diagnostic : diagnostics) {
        TPP_CHECK_EQ(
            diagnostic.message,
            std::string{"unknown escape sequence '\\q'"});
        TPP_CHECK(diagnostic.primary_span.has_value());
    }

    TPP_CHECK_EQ(diagnostics[0].primary_span->begin, std::size_t{1});
    TPP_CHECK_EQ(diagnostics[0].primary_span->end, std::size_t{3});
    TPP_CHECK_EQ(diagnostics[1].primary_span->begin, std::size_t{6});
    TPP_CHECK_EQ(diagnostics[1].primary_span->end, std::size_t{8});
}

void raw_newlines_discard_string_literals_and_resume_on_the_next_line()
{
    const LexingResult line_feed{"\"bad\nnext"};

    check_spelled_tokens(
        line_feed,
        {
            {TokenKind::identifier, "next"},
        });
    check_single_error(
        line_feed,
        "raw newline in string literal",
        4,
        5);

    const LexingResult crlf{"\"bad\r\nnext"};

    check_spelled_tokens(
        crlf,
        {
            {TokenKind::identifier, "next"},
        });
    check_single_error(
        crlf,
        "raw newline in string literal",
        4,
        6);
}

void unterminated_strings_and_trailing_backslashes_are_distinct()
{
    const LexingResult unterminated{"\"bad"};
    check_kinds(unterminated, {});
    check_single_error(
        unterminated,
        "unterminated string literal",
        0,
        4);

    const LexingResult trailing_backslash{"\"bad\\"};
    check_kinds(trailing_backslash, {});
    check_single_error(
        trailing_backslash,
        "trailing backslash in string literal",
        4,
        5);
}

void malformed_character_lengths_are_diagnosed_and_recover()
{
    const LexingResult empty{"'' next"};
    check_spelled_tokens(
        empty,
        {
            {TokenKind::identifier, "next"},
        });
    check_single_error(empty, "empty character literal", 0, 2);

    const LexingResult long_literal{"'ab' next"};
    check_spelled_tokens(
        long_literal,
        {
            {TokenKind::identifier, "next"},
        });
    check_single_error(
        long_literal,
        "character literal must contain exactly one byte",
        0,
        4);
}

void non_ascii_character_literal_is_rejected_by_byte()
{
    std::string source{"'"};
    source.push_back(static_cast<char>(0xff));
    source += "' next";
    const LexingResult result{std::move(source)};

    check_spelled_tokens(
        result,
        {
            {TokenKind::identifier, "next"},
        });
    check_single_error(
        result,
        "non-ASCII byte in character literal",
        1,
        2);
}

void raw_newlines_discard_character_literals_and_resume()
{
    const LexingResult line_feed{"'x\nnext"};

    check_spelled_tokens(
        line_feed,
        {
            {TokenKind::identifier, "next"},
        });
    check_single_error(
        line_feed,
        "raw newline in character literal",
        2,
        3);

    const LexingResult crlf{"'x\r\nnext"};

    check_spelled_tokens(
        crlf,
        {
            {TokenKind::identifier, "next"},
        });
    check_single_error(
        crlf,
        "raw newline in character literal",
        2,
        4);
}

void unterminated_characters_and_trailing_backslashes_are_distinct()
{
    const LexingResult unterminated{"'x"};
    check_kinds(unterminated, {});
    check_single_error(
        unterminated,
        "unterminated character literal",
        0,
        2);

    const LexingResult trailing_backslash{"'\\"};
    check_kinds(trailing_backslash, {});
    check_single_error(
        trailing_backslash,
        "trailing backslash in character literal",
        1,
        2);
}

void every_token_span_uses_the_selected_source_id()
{
    SourceManager sources;
    static_cast<void>(sources.add_source("first.tpp", "ignored"));
    const auto source = sources.add_source("second.tpp", "name + 1");
    DiagnosticEngine diagnostics;
    Lexer lexer{source, sources, diagnostics};
    const auto tokens = lexer.lex();

    TPP_CHECK_EQ(tokens.size(), std::size_t{4});
    for (const auto& token : tokens) {
        TPP_CHECK(token.span.source == source);
    }

    TPP_CHECK_EQ(tokens[0].kind, TokenKind::identifier);
    TPP_CHECK_EQ(tokens[1].kind, TokenKind::plus);
    TPP_CHECK_EQ(tokens[2].kind, TokenKind::integer_literal);
    TPP_CHECK_EQ(tokens[3].kind, TokenKind::end_of_file);
    TPP_CHECK(!diagnostics.has_errors());
}

void lexical_diagnostic_rendering_has_a_stable_full_format()
{
    const LexingResult result{"@", "invalid.tpp"};

    check_single_error(result, "unknown character '@'", 0, 1);
    TPP_CHECK_EQ(
        render(result),
        std::string{
            "invalid.tpp:1:1: error: unknown character '@'\n"
            "  1 | @\n"
            "    | ^\n"});
}

}

int main()
{
    return tpp::test::run({
        {"empty source produces exactly one EOF",
         empty_source_produces_exactly_one_eof},
        {"identifiers are ASCII and keep source spelling",
         identifiers_are_ascii_and_keep_source_spelling},
        {"all reserved words are classified",
         all_reserved_words_are_classified},
        {"boolean literals carry boolean values",
         boolean_literals_carry_boolean_values},
        {"integer literals remain unconverted source slices",
         integer_literals_remain_unconverted_source_slices},
        {"all punctuation is tokenized", all_punctuation_is_tokenized},
        {"all symbolic operators are tokenized",
         all_symbolic_operators_are_tokenized},
        {"operators use longest match", operators_use_longest_match},
        {"whitespace and line comments are ignored",
         whitespace_and_line_comments_are_ignored},
        {"simple string literals carry owned values",
         simple_string_literals_carry_owned_values},
        {"string literal decodes every supported escape",
         string_literal_decodes_every_supported_escape},
        {"strings preserve raw bytes and embedded NUL",
         strings_preserve_raw_bytes_and_embedded_nul},
        {"character literals decode every supported escape",
         character_literals_decode_every_supported_escape},
        {"raw NUL is a valid one-byte character literal",
         raw_nul_is_a_valid_one_byte_character_literal},
        {"unknown source characters report exact errors and make progress",
         unknown_source_characters_report_exact_errors_and_make_progress},
        {"independent lexical errors preserve tokens and order",
         independent_lexical_errors_preserve_tokens_and_source_order},
        {"lexer reuse is deterministic",
         lexer_instances_can_be_reused_deterministically},
        {"solitary ampersand and pipe have actionable diagnostics",
         solitary_ampersand_and_pipe_have_actionable_diagnostics},
        {"unknown escapes discard literals and recover after closing quote",
         unknown_escapes_discard_literals_and_recover_after_closing_quote},
        {"raw newlines discard strings and resume on next line",
         raw_newlines_discard_string_literals_and_resume_on_the_next_line},
        {"unterminated strings and trailing backslashes are distinct",
         unterminated_strings_and_trailing_backslashes_are_distinct},
        {"malformed character lengths are diagnosed and recover",
         malformed_character_lengths_are_diagnosed_and_recover},
        {"non-ASCII character literal is rejected by byte",
         non_ascii_character_literal_is_rejected_by_byte},
        {"raw newlines discard characters and resume",
         raw_newlines_discard_character_literals_and_resume},
        {"unterminated characters and trailing backslashes are distinct",
         unterminated_characters_and_trailing_backslashes_are_distinct},
        {"every token span uses selected source ID",
         every_token_span_uses_the_selected_source_id},
        {"lexical diagnostic rendering has a stable full format",
         lexical_diagnostic_rendering_has_a_stable_full_format},
    });
}

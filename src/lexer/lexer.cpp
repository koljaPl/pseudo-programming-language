#include "pseudo/lexer/lexer.hpp"

#include "pseudo/diagnostics/diagnostic_engine.hpp"
#include "pseudo/source/source_manager.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace tpp {
namespace {

struct ReservedWord {
    std::string_view spelling;
    TokenKind kind;
};

constexpr std::array reserved_words{
    ReservedWord{"int", TokenKind::keyword_int},
    ReservedWord{"bool", TokenKind::keyword_bool},
    ReservedWord{"char", TokenKind::keyword_char},
    ReservedWord{"string", TokenKind::keyword_string},
    ReservedWord{"void", TokenKind::keyword_void},
    ReservedWord{"vector", TokenKind::keyword_vector},
    ReservedWord{"if", TokenKind::keyword_if},
    ReservedWord{"else", TokenKind::keyword_else},
    ReservedWord{"while", TokenKind::keyword_while},
    ReservedWord{"for", TokenKind::keyword_for},
    ReservedWord{"in", TokenKind::keyword_in},
    ReservedWord{"return", TokenKind::keyword_return},
    ReservedWord{"break", TokenKind::keyword_break},
    ReservedWord{"continue", TokenKind::keyword_continue},
    ReservedWord{"and", TokenKind::logical_and},
    ReservedWord{"or", TokenKind::logical_or},
    ReservedWord{"not", TokenKind::logical_not},
};

constexpr bool is_ascii_letter(char character) noexcept {
    return (character >= 'A' && character <= 'Z')
        || (character >= 'a' && character <= 'z');
}

constexpr bool is_ascii_digit(char character) noexcept {
    return character >= '0' && character <= '9';
}

constexpr bool is_identifier_start(char character) noexcept {
    return is_ascii_letter(character) || character == '_';
}

constexpr bool is_identifier_continue(char character) noexcept {
    return is_identifier_start(character) || is_ascii_digit(character);
}

constexpr bool is_whitespace(char character) noexcept {
    return character == ' ' || character == '\t'
        || character == '\r' || character == '\n';
}

constexpr bool is_ascii(char character) noexcept {
    return static_cast<unsigned char>(character) <= 0x7f;
}

std::string hexadecimal_byte(unsigned char byte) {
    constexpr std::string_view digits{"0123456789ABCDEF"};

    std::string result(2, '0');
    result[0] = digits[byte >> 4];
    result[1] = digits[byte & 0x0f];
    return result;
}

std::optional<char> decode_escape(char code) noexcept {
    switch (code) {
    case '\\':
        return '\\';
    case '"':
        return '"';
    case '\'':
        return '\'';
    case 'n':
        return '\n';
    case 'r':
        return '\r';
    case 't':
        return '\t';
    case '0':
        return '\0';
    default:
        return std::nullopt;
    }
}

std::string unknown_character_message(char character) {
    const auto byte = static_cast<unsigned char>(character);

    if (byte >= 0x20 && byte <= 0x7e && character != '\''
        && character != '\\') {
        return std::string{"unknown character '"} + character + "'";
    }

    return std::string{"unknown byte 0x"} + hexadecimal_byte(byte);
}

std::string unknown_escape_message(char code) {
    const auto byte = static_cast<unsigned char>(code);

    if (byte >= 0x20 && byte <= 0x7e) {
        return std::string{"unknown escape sequence '\\"} + code + "'";
    }

    return std::string{"unknown escape byte 0x"} + hexadecimal_byte(byte);
}

}

Lexer::Lexer(
    SourceId source,
    const SourceManager& sources,
    DiagnosticEngine& diagnostics)
    : source_{source}
    , contents_{sources.contents(source)}
    , diagnostics_{diagnostics} {
}

std::vector<Token> Lexer::lex() {
    offset_ = 0;
    tokens_.clear();

    while (!at_end()) {
        if (is_whitespace(contents_[offset_])) {
            ++offset_;
            continue;
        }

        if (starts_with("//")) {
            skip_comment();
            continue;
        }

        const auto begin = offset_;

        if (is_identifier_start(contents_[offset_])) {
            lex_identifier(begin);
            continue;
        }

        if (is_ascii_digit(contents_[offset_])) {
            lex_integer(begin);
            continue;
        }

        const auto character = advance();

        switch (character) {
        case '"':
            lex_string(begin);
            break;
        case '\'':
            lex_character(begin);
            break;
        case '(':
            add_token(TokenKind::left_parenthesis, begin);
            break;
        case ')':
            add_token(TokenKind::right_parenthesis, begin);
            break;
        case '{':
            add_token(TokenKind::left_brace, begin);
            break;
        case '}':
            add_token(TokenKind::right_brace, begin);
            break;
        case '[':
            add_token(TokenKind::left_bracket, begin);
            break;
        case ']':
            add_token(TokenKind::right_bracket, begin);
            break;
        case ',':
            add_token(TokenKind::comma, begin);
            break;
        case ';':
            add_token(TokenKind::semicolon, begin);
            break;
        case '.':
            if (match('.')) {
                add_token(
                    match('=') ? TokenKind::range_inclusive
                               : TokenKind::range_exclusive,
                    begin);
            } else {
                add_token(TokenKind::dot, begin);
            }
            break;
        case '=':
            add_token(
                match('=') ? TokenKind::equal : TokenKind::assign,
                begin);
            break;
        case '+':
            add_token(
                match('=') ? TokenKind::plus_assign : TokenKind::plus,
                begin);
            break;
        case '-':
            add_token(
                match('=') ? TokenKind::minus_assign : TokenKind::minus,
                begin);
            break;
        case '*':
            add_token(
                match('=') ? TokenKind::star_assign : TokenKind::star,
                begin);
            break;
        case '/':
            add_token(
                match('=') ? TokenKind::slash_assign : TokenKind::slash,
                begin);
            break;
        case '%':
            add_token(
                match('=') ? TokenKind::percent_assign : TokenKind::percent,
                begin);
            break;
        case '!':
            add_token(
                match('=') ? TokenKind::not_equal : TokenKind::logical_not,
                begin);
            break;
        case '<':
            add_token(
                match('=') ? TokenKind::less_equal : TokenKind::less,
                begin);
            break;
        case '>':
            add_token(
                match('=') ? TokenKind::greater_equal : TokenKind::greater,
                begin);
            break;
        case '&':
            if (match('&')) {
                add_token(TokenKind::logical_and, begin);
            } else {
                report_error(
                    begin,
                    offset_,
                    "unexpected '&'; use '&&' for logical and");
            }
            break;
        case '|':
            if (match('|')) {
                add_token(TokenKind::logical_or, begin);
            } else {
                report_error(
                    begin,
                    offset_,
                    "unexpected '|'; use '||' for logical or");
            }
            break;
        default:
            report_error(
                begin,
                offset_,
                unknown_character_message(character));
            break;
        }
    }

    add_token(TokenKind::end_of_file, offset_);
    return std::move(tokens_);
}

bool Lexer::at_end() const noexcept {
    return offset_ >= contents_.size();
}

bool Lexer::starts_with(std::string_view text) const noexcept {
    return contents_.substr(offset_).starts_with(text);
}

char Lexer::advance() noexcept {
    return contents_[offset_++];
}

bool Lexer::match(char expected) noexcept {
    if (at_end() || contents_[offset_] != expected) {
        return false;
    }

    ++offset_;
    return true;
}

void Lexer::skip_comment() noexcept {
    offset_ += 2;

    while (!at_end() && contents_[offset_] != '\r'
           && contents_[offset_] != '\n') {
        ++offset_;
    }
}

void Lexer::consume_newline() noexcept {
    if (at_end()) {
        return;
    }

    if (contents_[offset_] == '\r') {
        ++offset_;
        if (!at_end() && contents_[offset_] == '\n') {
            ++offset_;
        }
        return;
    }

    if (contents_[offset_] == '\n') {
        ++offset_;
    }
}

void Lexer::lex_identifier(std::size_t begin) {
    while (!at_end() && is_identifier_continue(contents_[offset_])) {
        ++offset_;
    }

    const auto spelling = contents_.substr(begin, offset_ - begin);

    if (spelling == "true" || spelling == "false") {
        add_token(
            TokenKind::boolean_literal,
            begin,
            spelling == "true");
        return;
    }

    for (const auto& word : reserved_words) {
        if (spelling == word.spelling) {
            add_token(word.kind, begin);
            return;
        }
    }

    add_token(TokenKind::identifier, begin);
}

void Lexer::lex_integer(std::size_t begin) {
    while (!at_end() && is_ascii_digit(contents_[offset_])) {
        ++offset_;
    }

    add_token(TokenKind::integer_literal, begin);
}

void Lexer::lex_string(std::size_t begin) {
    std::string decoded;
    bool valid = true;

    while (!at_end()) {
        const auto character = contents_[offset_];

        if (character == '"') {
            ++offset_;
            if (valid) {
                add_token(
                    TokenKind::string_literal,
                    begin,
                    std::move(decoded));
            }
            return;
        }

        if (character == '\r' || character == '\n') {
            const auto newline_begin = offset_;
            consume_newline();
            report_error(
                newline_begin,
                offset_,
                "raw newline in string literal");
            return;
        }

        if (character == '\\') {
            if (!consume_escape(decoded, valid, "string literal")) {
                return;
            }
            continue;
        }

        decoded.push_back(character);
        ++offset_;
    }

    report_error(begin, offset_, "unterminated string literal");
}

void Lexer::lex_character(std::size_t begin) {
    std::string decoded;
    std::optional<std::size_t> non_ascii_offset;
    bool valid = true;

    while (!at_end()) {
        const auto character = contents_[offset_];

        if (character == '\'') {
            ++offset_;

            if (!valid) {
                return;
            }

            if (decoded.empty()) {
                report_error(begin, offset_, "empty character literal");
                return;
            }

            if (non_ascii_offset.has_value()) {
                report_error(
                    *non_ascii_offset,
                    *non_ascii_offset + 1,
                    "non-ASCII byte in character literal");
                return;
            }

            if (decoded.size() != 1) {
                report_error(
                    begin,
                    offset_,
                    "character literal must contain exactly one byte");
                return;
            }

            add_token(
                TokenKind::character_literal,
                begin,
                decoded.front());
            return;
        }

        if (character == '\r' || character == '\n') {
            const auto newline_begin = offset_;
            consume_newline();
            report_error(
                newline_begin,
                offset_,
                "raw newline in character literal");
            return;
        }

        if (character == '\\') {
            if (!consume_escape(decoded, valid, "character literal")) {
                return;
            }
            continue;
        }

        if (!is_ascii(character) && !non_ascii_offset.has_value()) {
            non_ascii_offset = offset_;
        }

        decoded.push_back(character);
        ++offset_;
    }

    report_error(begin, offset_, "unterminated character literal");
}

bool Lexer::consume_escape(
    std::string& decoded,
    bool& valid,
    std::string_view literal_name) {
    const auto escape_begin = offset_;
    ++offset_;

    if (at_end()) {
        report_error(
            escape_begin,
            offset_,
            "trailing backslash in " + std::string{literal_name});
        return false;
    }

    if (contents_[offset_] == '\r' || contents_[offset_] == '\n') {
        const auto newline_begin = offset_;
        consume_newline();
        report_error(
            newline_begin,
            offset_,
            "raw newline in " + std::string{literal_name});
        return false;
    }

    const auto code = advance();
    const auto value = decode_escape(code);

    if (!value.has_value()) {
        report_error(
            escape_begin,
            offset_,
            unknown_escape_message(code));
        valid = false;
        return true;
    }

    decoded.push_back(*value);
    return true;
}

void Lexer::add_token(
    TokenKind kind,
    std::size_t begin,
    TokenValue value) {
    tokens_.push_back(Token{
        .kind = kind,
        .span = SourceSpan{
            .source = source_,
            .begin = begin,
            .end = offset_,
        },
        .value = std::move(value),
    });
}

void Lexer::report_error(
    std::size_t begin,
    std::size_t end,
    std::string message) {
    diagnostics_.error(
        SourceSpan{
            .source = source_,
            .begin = begin,
            .end = end,
        },
        std::move(message));
}

}

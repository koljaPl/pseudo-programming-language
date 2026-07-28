#pragma once

#include "pseudo/lexer/token.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace tpp {

class DiagnosticEngine;
class SourceManager;

class Lexer {
public:
    Lexer(
        SourceId source,
        const SourceManager& sources,
        DiagnosticEngine& diagnostics);

    [[nodiscard]] std::vector<Token> lex();

private:
    [[nodiscard]] bool at_end() const noexcept;
    [[nodiscard]] bool starts_with(std::string_view text) const noexcept;
    [[nodiscard]] char advance() noexcept;
    [[nodiscard]] bool match(char expected) noexcept;

    void skip_comment() noexcept;
    void consume_newline() noexcept;
    void lex_identifier(std::size_t begin);
    void lex_integer(std::size_t begin);
    void lex_string(std::size_t begin);
    void lex_character(std::size_t begin);
    [[nodiscard]] bool consume_escape(
        std::string& decoded,
        bool& valid,
        std::string_view literal_name);

    void add_token(
        TokenKind kind,
        std::size_t begin,
        TokenValue value = std::monostate{});
    void report_error(
        std::size_t begin,
        std::size_t end,
        std::string message);

    SourceId source_;
    std::string_view contents_;
    DiagnosticEngine& diagnostics_;
    std::size_t offset_ = 0;
    std::vector<Token> tokens_;
};

}

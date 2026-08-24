#pragma once

#include "pseudo/common/source_span.hpp"

#include <string>
#include <variant>

namespace tpp {

enum class TokenKind {
    end_of_file,
    identifier,
    integer_literal,
    boolean_literal,
    string_literal,
    character_literal,

    keyword_int,
    keyword_bool,
    keyword_char,
    keyword_string,
    keyword_void,
    keyword_vector,
    keyword_if,
    keyword_else,
    keyword_while,
    keyword_for,
    keyword_in,
    keyword_return,
    keyword_break,
    keyword_continue,

    left_parenthesis,
    right_parenthesis,
    left_brace,
    right_brace,
    left_bracket,
    right_bracket,
    comma,
    semicolon,
    dot,

    assign,
    plus,
    minus,
    star,
    slash,
    percent,
    plus_assign,
    minus_assign,
    star_assign,
    slash_assign,
    percent_assign,
    equal,
    not_equal,
    less,
    less_equal,
    greater,
    greater_equal,
    logical_and,
    logical_or,
    logical_not,
    range_exclusive,
    range_inclusive,
};

using TokenValue = std::variant<std::monostate, bool, char, std::string>;

struct Token {
    TokenKind kind;
    SourceSpan span;

    // Boolean and decoded string/character literals carry values.
    // Identifier and integer spellings remain in the source buffer.
    TokenValue value;
};

}

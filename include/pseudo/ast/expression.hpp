#pragma once

#include "pseudo/common/source_span.hpp"

#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace tpp {

enum class ScalarTypeKind {
    integer,
    boolean,
    character,
    string,
};

struct VectorType;

using VectorElementType =
    std::variant<ScalarTypeKind, std::unique_ptr<VectorType>>;

struct VectorType {
    SourceSpan span;
    VectorElementType element_type;
};

enum class UnaryOperator {
    plus,
    minus,
    logical_not,
};

enum class BinaryOperator {
    logical_or,
    logical_and,
    equal,
    not_equal,
    less,
    less_equal,
    greater,
    greater_equal,
    add,
    subtract,
    multiply,
    divide,
    remainder,
};

struct Expression;

using ExpressionPtr = std::unique_ptr<Expression>;

struct IntegerLiteralExpression {
    std::string lexeme;
};

struct BooleanLiteralExpression {
    bool value;
};

struct CharacterLiteralExpression {
    char value;
};

struct StringLiteralExpression {
    std::string value;
};

struct IdentifierExpression {
    std::string name;
};

struct UnaryExpression {
    UnaryOperator operator_kind;
    ExpressionPtr operand;
};

struct BinaryExpression {
    BinaryOperator operator_kind;
    ExpressionPtr left;
    ExpressionPtr right;
};

struct CallExpression {
    ExpressionPtr callee;
    std::vector<ExpressionPtr> arguments;
};

struct IndexExpression {
    ExpressionPtr base;
    ExpressionPtr index;
};

struct MemberAccessExpression {
    ExpressionPtr base;
    std::string member;
};

struct VectorConstructionExpression {
    VectorType type;
    std::vector<ExpressionPtr> arguments;
};

struct ParenthesizedExpression {
    ExpressionPtr expression;
};

using ExpressionNode = std::variant<
    IntegerLiteralExpression,
    BooleanLiteralExpression,
    CharacterLiteralExpression,
    StringLiteralExpression,
    IdentifierExpression,
    UnaryExpression,
    BinaryExpression,
    CallExpression,
    IndexExpression,
    MemberAccessExpression,
    VectorConstructionExpression,
    ParenthesizedExpression>;

struct Expression {
    SourceSpan span;
    ExpressionNode node;
};

}

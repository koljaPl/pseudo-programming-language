#pragma once

#include "pseudo/ast/expression.hpp"
#include "pseudo/ast/type.hpp"

#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace tpp {

struct Block;

using BlockPtr = std::unique_ptr<Block>;

struct Parameter {
    SourceSpan span;
    ValueType type;
    std::string name;
    SourceSpan name_span;
};

struct VariableDeclaration {
    SourceSpan span;
    ValueType type;
    std::string name;
    SourceSpan name_span;
    ExpressionPtr initializer;
};

struct FunctionDeclaration {
    SourceSpan span;
    ReturnType return_type;
    std::string name;
    SourceSpan name_span;
    std::vector<Parameter> parameters;
    BlockPtr body;
};

enum class AssignmentOperator {
    assign,
    add_assign,
    subtract_assign,
    multiply_assign,
    divide_assign,
    remainder_assign,
};

struct AssignmentTarget {
    SourceSpan span;
    std::string name;
    SourceSpan name_span;
    std::vector<ExpressionPtr> indices;
};

struct AssignmentStatement {
    AssignmentTarget target;
    AssignmentOperator operator_kind;
    ExpressionPtr value;
};

struct ExpressionStatement {
    ExpressionPtr expression;
};

struct IfStatement {
    ExpressionPtr condition;
    BlockPtr then_block;
    BlockPtr else_block;
};

struct WhileStatement {
    ExpressionPtr condition;
    BlockPtr body;
};

enum class RangeOperator {
    exclusive,
    inclusive,
};

struct ForRangeStatement {
    std::string variable;
    SourceSpan variable_span;
    ExpressionPtr begin;
    RangeOperator operator_kind;
    ExpressionPtr end;
    BlockPtr body;
};

struct ForEachStatement {
    std::string variable;
    SourceSpan variable_span;
    ExpressionPtr iterable;
    BlockPtr body;
};

struct ReturnStatement {
    ExpressionPtr value;
};

struct BreakStatement {};

struct ContinueStatement {};

struct BlockStatement {
    BlockPtr block;
};

using StatementNode = std::variant<
    VariableDeclaration,
    AssignmentStatement,
    ExpressionStatement,
    IfStatement,
    WhileStatement,
    ForRangeStatement,
    ForEachStatement,
    ReturnStatement,
    BreakStatement,
    ContinueStatement,
    BlockStatement>;

struct Statement {
    SourceSpan span;
    StatementNode node;
};

using BlockItem = std::variant<FunctionDeclaration, Statement>;

struct Block {
    SourceSpan span;
    std::vector<BlockItem> items;
};

using TopLevelDeclaration =
    std::variant<FunctionDeclaration, VariableDeclaration>;

struct Program {
    SourceSpan span;
    std::vector<TopLevelDeclaration> declarations;
};

}

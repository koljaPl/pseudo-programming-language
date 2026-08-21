#pragma once

#include "pseudo/common/source_span.hpp"
#include "pseudo/semantic/builtin.hpp"
#include "pseudo/semantic/member_kind.hpp"
#include "pseudo/semantic/symbol_table.hpp"
#include "pseudo/semantic/type_context.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace tpp {

struct TempId {
    std::size_t value;

    constexpr bool operator==(const TempId& other) const noexcept {
        return value == other.value;
    }
};

enum class TempRole {
    range_begin,
    range_end,
    range_cursor,
    range_active,
    iterable_snapshot,
};

using LoweredStorage = std::variant<SymbolId, TempId>;

struct LoweredTemporary {
    TempId id;
    TempRole role;
    SymbolId owner;
    TypeId type;
    SourceSpan span;
};

enum class LoweredUnaryOperator {
    plus,
    minus,
    logical_not,
};

enum class LoweredBinaryOperator {
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

enum class LoweredAssignmentOperator {
    assign,
    add_assign,
    subtract_assign,
    multiply_assign,
    divide_assign,
    remainder_assign,
};

struct LoweredExpression;
struct LoweredBlock;

using LoweredExpressionPtr = std::unique_ptr<LoweredExpression>;
using LoweredBlockPtr = std::unique_ptr<LoweredBlock>;

struct LoweredIntegerLiteralExpression {
    std::string lexeme;
};

struct LoweredBooleanLiteralExpression {
    bool value;
};

struct LoweredCharacterLiteralExpression {
    char value;
};

struct LoweredStringLiteralExpression {
    std::string value;
};

struct LoweredStorageExpression {
    LoweredStorage storage;
};

struct LoweredUnaryExpression {
    LoweredUnaryOperator operator_kind;
    LoweredExpressionPtr operand;
};

struct LoweredBinaryExpression {
    LoweredBinaryOperator operator_kind;
    LoweredExpressionPtr left;
    LoweredExpressionPtr right;
};

struct LoweredUserCallExpression {
    SymbolId function;
    SourceSpan callee_span;
    std::vector<LoweredExpressionPtr> arguments;
};

struct LoweredBuiltinCallExpression {
    BuiltinFunctionKind builtin;
    SourceSpan callee_span;
    std::vector<LoweredExpressionPtr> arguments;
};

struct LoweredMemberCallExpression {
    MemberKind member;
    SourceSpan member_span;
    LoweredExpressionPtr receiver;
    std::vector<LoweredExpressionPtr> arguments;
};

struct LoweredIndexExpression {
    TypeId container_type;
    LoweredExpressionPtr base;
    LoweredExpressionPtr index;
};

struct LoweredVectorConstructionExpression {
    TypeId element_type;
    std::vector<LoweredExpressionPtr> arguments;
};

struct LoweredGroupedExpression {
    LoweredExpressionPtr expression;
};

using LoweredExpressionNode = std::variant<
    LoweredIntegerLiteralExpression,
    LoweredBooleanLiteralExpression,
    LoweredCharacterLiteralExpression,
    LoweredStringLiteralExpression,
    LoweredStorageExpression,
    LoweredUnaryExpression,
    LoweredBinaryExpression,
    LoweredUserCallExpression,
    LoweredBuiltinCallExpression,
    LoweredMemberCallExpression,
    LoweredIndexExpression,
    LoweredVectorConstructionExpression,
    LoweredGroupedExpression>;

struct LoweredExpression {
    SourceSpan span;
    TypeId type;
    LoweredExpressionNode node;
};

struct LoweredVariableStatement {
    SymbolId symbol;
    SourceSpan name_span;
    TypeId type;
    LoweredExpressionPtr initializer;
};

struct LoweredAssignmentTarget {
    SourceSpan span;
    SourceSpan storage_span;
    LoweredStorage storage;
    TypeId storage_type;
    TypeId type;
    std::vector<TypeId> container_types;
    std::vector<LoweredExpressionPtr> indices;
};

struct LoweredAssignmentStatement {
    LoweredAssignmentTarget target;
    LoweredAssignmentOperator operator_kind;
    LoweredExpressionPtr value;
};

struct LoweredExpressionStatement {
    LoweredExpressionPtr expression;
};

struct LoweredReturnStatement {
    LoweredExpressionPtr value;
};

struct LoweredBreakStatement {};

struct LoweredContinueStatement {};

struct LoweredBlockStatement {
    LoweredBlockPtr block;
};

enum class LoweredRangeConditionKind {
    cursor_less_than_end,
    active,
};

enum class LoweredRangeStepKind {
    increment_cursor,
    update_active_then_guarded_increment,
};

struct LoweredRangeStatement {
    SymbolId binding;
    SourceSpan binding_span;
    TypeId binding_type;
    LoweredTemporary begin_storage;
    LoweredExpressionPtr begin_value;
    LoweredTemporary end_storage;
    LoweredExpressionPtr end_value;
    LoweredTemporary cursor_storage;
    std::optional<LoweredTemporary> active_storage;
    LoweredRangeConditionKind condition_kind;
    LoweredRangeStepKind step_kind;
    LoweredBlockPtr body;
};

struct LoweredForEachStatement {
    SymbolId binding;
    SourceSpan binding_span;
    TypeId binding_type;
    LoweredTemporary snapshot_storage;
    LoweredExpressionPtr iterable;
    TypeId iterable_type;
    LoweredBlockPtr body;
};

using LoweredStatementNode = std::variant<
    LoweredVariableStatement,
    LoweredAssignmentStatement,
    LoweredExpressionStatement,
    LoweredReturnStatement,
    LoweredBreakStatement,
    LoweredContinueStatement,
    LoweredBlockStatement,
    LoweredRangeStatement,
    LoweredForEachStatement>;

struct LoweredStatement {
    SourceSpan span;
    LoweredStatementNode node;
};

struct LoweredBlock {
    SourceSpan span;
    std::vector<LoweredStatement> statements;
};

struct LoweredParameter {
    SourceSpan span;
    SourceSpan name_span;
    SymbolId symbol;
    TypeId type;
};

struct LoweredFunction {
    SourceSpan span;
    SourceSpan name_span;
    SymbolId symbol;
    TypeId return_type;
    std::vector<LoweredParameter> parameters;
    LoweredBlockPtr body;
    bool is_main;
};

// Owns every lowered node and is independent of the source AST and its
// pointer-keyed side tables. Stored TypeIds remain handles into the
// originating TypeContext.
struct LoweredProgram {
    SourceSpan span;
    std::vector<LoweredFunction> functions;

    explicit LoweredProgram(
        SourceSpan source_span,
        std::vector<LoweredFunction> lowered_functions = {})
        : span{source_span}
        , functions{std::move(lowered_functions)}
    {}

    LoweredProgram(const LoweredProgram&) = delete;
    LoweredProgram& operator=(const LoweredProgram&) = delete;
    LoweredProgram(LoweredProgram&&) noexcept = default;
    LoweredProgram& operator=(LoweredProgram&&) noexcept = default;
};

}

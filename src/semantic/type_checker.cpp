#include "pseudo/semantic/type_checker.hpp"

#include "pseudo/diagnostics/diagnostic_engine.hpp"
#include "pseudo/semantic/ast_type.hpp"
#include "pseudo/semantic/builtin.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace tpp {
namespace {

constexpr std::string_view maximum_integer_magnitude =
    "9223372036854775807";
constexpr std::string_view minimum_integer_magnitude =
    "9223372036854775808";

[[nodiscard]] std::optional<std::string_view> normalized_integer(
    const std::string_view lexeme) noexcept {
    if (lexeme.empty()
        || !std::all_of(
            lexeme.begin(),
            lexeme.end(),
            [](const char character) {
                return character >= '0' && character <= '9';
            })) {
        return std::nullopt;
    }

    const auto first_non_zero = lexeme.find_first_not_of('0');
    return first_non_zero == std::string_view::npos
        ? std::optional<std::string_view>{lexeme.substr(lexeme.size() - 1)}
        : std::optional<std::string_view>{lexeme.substr(first_non_zero)};
}

[[nodiscard]] bool exceeds_magnitude(
    const std::string_view value,
    const std::string_view maximum) noexcept {
    return value.size() > maximum.size()
        || (value.size() == maximum.size() && value > maximum);
}

[[nodiscard]] const IntegerLiteralExpression* unwrap_integer_literal(
    const Expression& expression) noexcept {
    if (const auto* literal =
            std::get_if<IntegerLiteralExpression>(&expression.node)) {
        return literal;
    }

    const auto* parentheses =
        std::get_if<ParenthesizedExpression>(&expression.node);
    if (parentheses == nullptr || parentheses->expression == nullptr) {
        return nullptr;
    }

    return unwrap_integer_literal(*parentheses->expression);
}

[[nodiscard]] bool is_minimum_integer_value(
    const Expression& expression) noexcept {
    if (const auto* parentheses =
            std::get_if<ParenthesizedExpression>(&expression.node)) {
        return parentheses->expression != nullptr
            && is_minimum_integer_value(*parentheses->expression);
    }

    const auto* unary = std::get_if<UnaryExpression>(&expression.node);
    if (unary == nullptr || unary->operand == nullptr) {
        return false;
    }

    if (unary->operator_kind == UnaryOperator::plus) {
        return is_minimum_integer_value(*unary->operand);
    }
    if (unary->operator_kind != UnaryOperator::minus) {
        return false;
    }

    const auto* literal = unwrap_integer_literal(*unary->operand);
    if (literal == nullptr) {
        return false;
    }

    const auto magnitude = normalized_integer(literal->lexeme);
    return magnitude.has_value()
        && *magnitude == minimum_integer_magnitude;
}

[[nodiscard]] bool is_global_constant_expression(
    const Expression& expression) noexcept {
    return std::visit(
        [](const auto& node) -> bool {
            using Node = std::decay_t<decltype(node)>;

            if constexpr (
                std::is_same_v<Node, IntegerLiteralExpression>
                || std::is_same_v<Node, BooleanLiteralExpression>
                || std::is_same_v<Node, CharacterLiteralExpression>
                || std::is_same_v<Node, StringLiteralExpression>) {
                return true;
            } else if constexpr (std::is_same_v<Node, UnaryExpression>) {
                return node.operand != nullptr
                    && is_global_constant_expression(*node.operand);
            } else if constexpr (std::is_same_v<Node, BinaryExpression>) {
                return node.left != nullptr && node.right != nullptr
                    && is_global_constant_expression(*node.left)
                    && is_global_constant_expression(*node.right);
            } else if constexpr (
                std::is_same_v<Node, ParenthesizedExpression>) {
                return node.expression != nullptr
                    && is_global_constant_expression(*node.expression);
            } else {
                return false;
            }
        },
        expression.node);
}

[[nodiscard]] std::string_view unary_operator_name(
    const UnaryOperator operator_kind) noexcept {
    switch (operator_kind) {
    case UnaryOperator::plus:
        return "+";
    case UnaryOperator::minus:
        return "-";
    case UnaryOperator::logical_not:
        return "!";
    }
    return "?";
}

[[nodiscard]] std::string_view binary_operator_name(
    const BinaryOperator operator_kind) noexcept {
    switch (operator_kind) {
    case BinaryOperator::logical_or:
        return "||";
    case BinaryOperator::logical_and:
        return "&&";
    case BinaryOperator::equal:
        return "==";
    case BinaryOperator::not_equal:
        return "!=";
    case BinaryOperator::less:
        return "<";
    case BinaryOperator::less_equal:
        return "<=";
    case BinaryOperator::greater:
        return ">";
    case BinaryOperator::greater_equal:
        return ">=";
    case BinaryOperator::add:
        return "+";
    case BinaryOperator::subtract:
        return "-";
    case BinaryOperator::multiply:
        return "*";
    case BinaryOperator::divide:
        return "/";
    case BinaryOperator::remainder:
        return "%";
    }
    return "?";
}

[[nodiscard]] bool is_arithmetic_operator(
    const BinaryOperator operator_kind) noexcept {
    return operator_kind == BinaryOperator::add
        || operator_kind == BinaryOperator::subtract
        || operator_kind == BinaryOperator::multiply
        || operator_kind == BinaryOperator::divide
        || operator_kind == BinaryOperator::remainder;
}

[[nodiscard]] bool is_logical_operator(
    const BinaryOperator operator_kind) noexcept {
    return operator_kind == BinaryOperator::logical_and
        || operator_kind == BinaryOperator::logical_or;
}

[[nodiscard]] bool is_equality_operator(
    const BinaryOperator operator_kind) noexcept {
    return operator_kind == BinaryOperator::equal
        || operator_kind == BinaryOperator::not_equal;
}

[[nodiscard]] bool is_ordering_operator(
    const BinaryOperator operator_kind) noexcept {
    return operator_kind == BinaryOperator::less
        || operator_kind == BinaryOperator::less_equal
        || operator_kind == BinaryOperator::greater
        || operator_kind == BinaryOperator::greater_equal;
}

[[nodiscard]] const MemberAccessExpression* unwrap_member_access(
    const Expression& expression) noexcept {
    if (const auto* member =
            std::get_if<MemberAccessExpression>(&expression.node)) {
        return member;
    }

    const auto* parenthesized =
        std::get_if<ParenthesizedExpression>(&expression.node);
    if (parenthesized == nullptr || parenthesized->expression == nullptr) {
        return nullptr;
    }

    return unwrap_member_access(*parenthesized->expression);
}

[[nodiscard]] std::string parameter_type_name(
    const BuiltinParameterKind parameter) {
    switch (parameter) {
    case BuiltinParameterKind::integer:
        return "int";
    case BuiltinParameterKind::character:
        return "char";
    case BuiltinParameterKind::string:
        return "string";
    case BuiltinParameterKind::printable_scalar:
        return "int, bool, char, or string";
    }
    return "valid value";
}

}

struct TypeChecker::Callable {
    std::variant<SymbolId, BuiltinFunctionKind, MemberKind> target;
};

struct TypeChecker::ExpressionResult {
    std::optional<TypeId> type;
    std::optional<Callable> callable;

    [[nodiscard]] static ExpressionResult error() noexcept {
        return {};
    }

    [[nodiscard]] static ExpressionResult value(const TypeId value_type) {
        return ExpressionResult{
            .type = value_type,
            .callable = std::nullopt,
        };
    }

    [[nodiscard]] static ExpressionResult callable_value(Callable value) {
        return ExpressionResult{
            .type = std::nullopt,
            .callable = std::move(value),
        };
    }

    [[nodiscard]] bool failed() const noexcept {
        return !type.has_value() && !callable.has_value();
    }
};

TypeChecker::TypeChecker(
    TypeContext& types,
    const SymbolTable& symbols,
    const DeclarationInfo& declarations,
    const ResolutionInfo& resolutions,
    TypeInfo& type_info,
    DiagnosticEngine& diagnostics)
    : types_{types},
      symbols_{symbols},
      declarations_{declarations},
      resolutions_{resolutions},
      type_info_{type_info},
      diagnostics_{diagnostics} {}

bool TypeChecker::check(const Program& program) {
    const auto initial_error_count = diagnostics_.error_count();

    for (const auto& declaration : program.declarations) {
        check_top_level(declaration);
    }

    return diagnostics_.error_count() == initial_error_count;
}

void TypeChecker::check_top_level(
    const TopLevelDeclaration& declaration) {
    std::visit(
        [this](const auto& node) {
            using Node = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<Node, FunctionDeclaration>) {
                check_function(node);
            } else {
                check_variable(node, true);
            }
        },
        declaration);
}

void TypeChecker::check_function(
    const FunctionDeclaration& declaration) {
    const auto previous_return_type = current_return_type_;
    current_return_type_.reset();

    if (const auto symbol = declarations_.symbol_for(declaration)) {
        const auto& data = symbols_.symbol(*symbol).data;
        if (const auto* function = std::get_if<FunctionSymbol>(&data)) {
            current_return_type_ = function->return_type;
        }
    }

    if (!current_return_type_.has_value()) {
        diagnostics_.error(
            declaration.name_span,
            "malformed semantic state: function has no signature");
    }

    if (declaration.body == nullptr) {
        diagnostics_.error(
            declaration.span,
            "malformed AST: function is missing a body");
    } else {
        check_block(*declaration.body);
    }

    current_return_type_ = previous_return_type;
}

void TypeChecker::check_block(const Block& block) {
    for (const auto& item : block.items) {
        check_block_item(item);
    }
}

void TypeChecker::check_block_item(const BlockItem& item) {
    std::visit(
        [this](const auto& node) {
            using Node = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<Node, FunctionDeclaration>) {
                check_function(node);
            } else {
                check_statement(node);
            }
        },
        item);
}

void TypeChecker::check_statement(const Statement& statement) {
    std::visit(
        [this, span = statement.span](const auto& node) {
            using Node = std::decay_t<decltype(node)>;

            if constexpr (std::is_same_v<Node, VariableDeclaration>) {
                check_variable(node, false);
            } else if constexpr (std::is_same_v<Node, AssignmentStatement>) {
                check_assignment(node, span);
            } else if constexpr (std::is_same_v<Node, ExpressionStatement>) {
                const auto result =
                    check_required_expression(node.expression, span);
                if (result.callable.has_value()) {
                    diagnostics_.error(
                        node.expression != nullptr
                            ? node.expression->span
                            : span,
                        "function or member function must be called");
                }
            } else if constexpr (std::is_same_v<Node, IfStatement>) {
                check_condition(node.condition, span);
                check_child_block(node.then_block, span);
                if (node.else_block != nullptr) {
                    check_child_block(node.else_block, span);
                }
            } else if constexpr (std::is_same_v<Node, WhileStatement>) {
                check_condition(node.condition, span);
                check_child_block(node.body, span);
            } else if constexpr (std::is_same_v<Node, ForRangeStatement>) {
                check_for_range(node, span);
            } else if constexpr (std::is_same_v<Node, ForEachStatement>) {
                check_for_each(node, span);
            } else if constexpr (std::is_same_v<Node, ReturnStatement>) {
                if (!current_return_type_.has_value()) {
                    diagnostics_.error(
                        span,
                        "malformed semantic state: return outside a function");
                    return;
                }

                if (node.value == nullptr) {
                    if (*current_return_type_ != types_.void_type()) {
                        diagnostics_.error(
                            span,
                            "non-void function must return a value of type '"
                                + type_name(*current_return_type_) + "'");
                    }
                    return;
                }

                const auto value = check_expression(*node.value);
                if (!value.type.has_value()) {
                    if (value.callable.has_value()) {
                        diagnostics_.error(
                            node.value->span,
                            "return expression must be a value");
                    }
                    return;
                }

                if (*current_return_type_ == types_.void_type()) {
                    diagnostics_.error(
                        node.value->span,
                        "void function cannot return a value of type '"
                            + type_name(*value.type) + "'");
                } else if (*value.type != *current_return_type_) {
                    report_type_mismatch(
                        node.value->span,
                        "cannot return",
                        *current_return_type_,
                        *value.type);
                }
            } else if constexpr (std::is_same_v<Node, BlockStatement>) {
                check_child_block(node.block, span);
            }
        },
        statement.node);
}

void TypeChecker::check_variable(
    const VariableDeclaration& declaration,
    const bool is_global) {
    if (declaration.initializer == nullptr) {
        return;
    }

    if (is_global
        && !is_global_constant_expression(*declaration.initializer)) {
        diagnostics_.error(
            declaration.initializer->span,
            "global variable initializer must be a constant expression");
        return;
    }

    const auto symbol = declarations_.symbol_for(declaration);
    if (!symbol.has_value()) {
        diagnostics_.error(
            declaration.name_span,
            "malformed semantic state: variable has no symbol");
        return;
    }

    const auto expected = type_of_symbol(*symbol);
    const auto actual = check_expression(*declaration.initializer);
    if (!expected.has_value() || actual.failed()) {
        return;
    }
    if (actual.callable.has_value()) {
        diagnostics_.error(
            declaration.initializer->span,
            "variable initializer must be a value");
        return;
    }
    if (*expected != *actual.type) {
        diagnostics_.error(
            declaration.initializer->span,
            "cannot initialize '" + type_name(*expected)
                + "' with value of type '" + type_name(*actual.type) + "'");
    }
}

void TypeChecker::check_assignment(
    const AssignmentStatement& statement,
    const SourceSpan span) {
    const auto target_type = check_assignment_target(statement.target);
    const auto value = check_required_expression(statement.value, span);
    if (!target_type.has_value() || value.failed()) {
        return;
    }
    if (value.callable.has_value()) {
        diagnostics_.error(
            statement.value != nullptr ? statement.value->span : span,
            "assignment value must be a value");
        return;
    }

    const auto value_span =
        statement.value != nullptr ? statement.value->span : span;
    if (statement.operator_kind == AssignmentOperator::assign) {
        if (*target_type != *value.type) {
            diagnostics_.error(
                value_span,
                "cannot assign value of type '" + type_name(*value.type)
                    + "' to target of type '" + type_name(*target_type)
                    + "'");
        }
        return;
    }

    auto allows_string_operands = false;
    switch (statement.operator_kind) {
    case AssignmentOperator::assign:
        return;
    case AssignmentOperator::add_assign:
        allows_string_operands = true;
        break;
    case AssignmentOperator::subtract_assign:
    case AssignmentOperator::multiply_assign:
    case AssignmentOperator::divide_assign:
    case AssignmentOperator::remainder_assign:
        break;
    default:
        diagnostics_.error(
            span,
            "malformed AST: unknown assignment operator");
        return;
    }

    const auto string_addition = allows_string_operands
        && *target_type == types_.string_type()
        && *value.type == types_.string_type();
    const auto integer_arithmetic =
        *target_type == types_.integer_type()
        && *value.type == types_.integer_type();
    if (!string_addition && !integer_arithmetic) {
        diagnostics_.error(
            value_span,
            "compound assignment requires matching 'int' operands, or "
            "'string' operands for '+='");
    }
}

void TypeChecker::check_for_range(
    const ForRangeStatement& statement,
    const SourceSpan span) {
    for (const auto* bound : {&statement.begin, &statement.end}) {
        const auto result = check_required_expression(*bound, span);
        if (result.type.has_value()
            && *result.type != types_.integer_type()) {
            diagnostics_.error(
                (*bound)->span,
                "for-range bound must have type 'int', got '"
                    + type_name(*result.type) + "'");
        } else if (result.callable.has_value()) {
            diagnostics_.error((*bound)->span, "for-range bound must be a value");
        }
    }

    check_child_block(statement.body, span);
}

void TypeChecker::check_for_each(
    const ForEachStatement& statement,
    const SourceSpan span) {
    const auto iterable = check_required_expression(statement.iterable, span);
    auto binding_type = std::optional<TypeId>{};

    if (iterable.type.has_value()) {
        if (*iterable.type == types_.string_type()) {
            binding_type = types_.character_type();
        } else if (const auto descriptor = types_.lookup(*iterable.type)) {
            if (const auto* vector =
                    std::get_if<SemanticVectorType>(&*descriptor)) {
                binding_type = vector->element_type;
            }
        }

        if (!binding_type.has_value()) {
            diagnostics_.error(
                statement.iterable != nullptr
                    ? statement.iterable->span
                    : span,
                "for-each iterable must have type 'string' or 'vector<T>', got '"
                    + type_name(*iterable.type) + "'");
        }
    } else if (iterable.callable.has_value()) {
        diagnostics_.error(
            statement.iterable != nullptr ? statement.iterable->span : span,
            "for-each iterable must be a value");
    }

    if (binding_type.has_value()) {
        if (const auto symbol = declarations_.symbol_for(statement)) {
            type_info_.record(*symbol, *binding_type);
        } else {
            diagnostics_.error(
                statement.variable_span,
                "malformed semantic state: for-each binding has no symbol");
        }
    }

    check_child_block(statement.body, span);
}

TypeChecker::ExpressionResult TypeChecker::check_expression(
    const Expression& expression) {
    return check_expression(expression, false);
}

TypeChecker::ExpressionResult TypeChecker::check_expression(
    const Expression& expression,
    const bool allow_minimum_integer_magnitude) {
    auto result = std::visit(
        [this, &expression, allow_minimum_integer_magnitude](
            const auto& node) -> ExpressionResult {
            using Node = std::decay_t<decltype(node)>;

            if constexpr (std::is_same_v<Node, IntegerLiteralExpression>) {
                const auto magnitude = normalized_integer(node.lexeme);
                if (!magnitude.has_value()) {
                    diagnostics_.error(
                        expression.span,
                        "malformed AST: invalid integer literal lexeme");
                    return ExpressionResult::error();
                }

                const auto maximum = allow_minimum_integer_magnitude
                    ? minimum_integer_magnitude
                    : maximum_integer_magnitude;
                if (exceeds_magnitude(*magnitude, maximum)) {
                    diagnostics_.error(
                        expression.span,
                        "integer literal is outside the signed 64-bit range");
                    return ExpressionResult::error();
                }

                return ExpressionResult::value(types_.integer_type());
            } else if constexpr (
                std::is_same_v<Node, BooleanLiteralExpression>) {
                return ExpressionResult::value(types_.boolean_type());
            } else if constexpr (
                std::is_same_v<Node, CharacterLiteralExpression>) {
                return ExpressionResult::value(types_.character_type());
            } else if constexpr (
                std::is_same_v<Node, StringLiteralExpression>) {
                return ExpressionResult::value(types_.string_type());
            } else if constexpr (std::is_same_v<Node, IdentifierExpression>) {
                return check_identifier(expression, node);
            } else if constexpr (std::is_same_v<Node, UnaryExpression>) {
                if (node.operand == nullptr) {
                    diagnostics_.error(
                        expression.span,
                        "malformed AST: unary expression is missing an operand");
                    return ExpressionResult::error();
                }

                auto allow_minimum = false;
                if (node.operator_kind == UnaryOperator::minus) {
                    if (const auto* integer =
                            unwrap_integer_literal(*node.operand)) {
                        const auto magnitude =
                            normalized_integer(integer->lexeme);
                        allow_minimum = magnitude.has_value()
                            && *magnitude == minimum_integer_magnitude;
                    }
                }

                const auto operand =
                    check_expression(*node.operand, allow_minimum);
                if (operand.failed()) {
                    return ExpressionResult::error();
                }
                if (operand.callable.has_value()) {
                    diagnostics_.error(
                        expression.span,
                        "unary operator '"
                            + std::string{unary_operator_name(
                                node.operator_kind)}
                            + "' requires a value operand");
                    return ExpressionResult::error();
                }

                if (node.operator_kind == UnaryOperator::minus
                    && is_minimum_integer_value(*node.operand)) {
                    diagnostics_.error(
                        expression.span,
                        "integer expression is outside the signed 64-bit range");
                    return ExpressionResult::error();
                }

                auto expected = types_.integer_type();
                switch (node.operator_kind) {
                case UnaryOperator::plus:
                case UnaryOperator::minus:
                    break;
                case UnaryOperator::logical_not:
                    expected = types_.boolean_type();
                    break;
                default:
                    diagnostics_.error(
                        expression.span,
                        "malformed AST: unknown unary operator");
                    return ExpressionResult::error();
                }
                if (*operand.type != expected) {
                    diagnostics_.error(
                        expression.span,
                        "unary operator '"
                            + std::string{unary_operator_name(
                                node.operator_kind)}
                            + "' requires '" + type_name(expected)
                            + "', got '" + type_name(*operand.type) + "'");
                    return ExpressionResult::error();
                }

                return ExpressionResult::value(expected);
            } else if constexpr (std::is_same_v<Node, BinaryExpression>) {
                if (node.left == nullptr || node.right == nullptr) {
                    diagnostics_.error(
                        expression.span,
                        "malformed AST: binary expression is missing an operand");
                    return ExpressionResult::error();
                }

                const auto left = check_expression(*node.left);
                const auto right = check_expression(*node.right);
                if (left.failed() || right.failed()) {
                    return ExpressionResult::error();
                }
                if (left.callable.has_value() || right.callable.has_value()) {
                    diagnostics_.error(
                        expression.span,
                        "operator '"
                            + std::string{binary_operator_name(
                                node.operator_kind)}
                            + "' requires value operands");
                    return ExpressionResult::error();
                }

                const auto operator_name =
                    std::string{binary_operator_name(node.operator_kind)};
                if (node.operator_kind == BinaryOperator::add) {
                    if (*left.type == types_.integer_type()
                        && *right.type == types_.integer_type()) {
                        return ExpressionResult::value(types_.integer_type());
                    }
                    if (*left.type == types_.string_type()
                        && *right.type == types_.string_type()) {
                        return ExpressionResult::value(types_.string_type());
                    }

                    diagnostics_.error(
                        expression.span,
                        "operator '+' requires two matching 'int' or "
                        "'string' operands, got '"
                            + type_name(*left.type) + "' and '"
                            + type_name(*right.type) + "'");
                    return ExpressionResult::error();
                }

                if (is_arithmetic_operator(node.operator_kind)) {
                    if (*left.type != types_.integer_type()
                        || *right.type != types_.integer_type()) {
                        diagnostics_.error(
                            expression.span,
                            "operator '" + operator_name
                                + "' requires two 'int' operands, got '"
                                + type_name(*left.type) + "' and '"
                                + type_name(*right.type) + "'");
                        return ExpressionResult::error();
                    }
                    return ExpressionResult::value(types_.integer_type());
                }

                if (is_logical_operator(node.operator_kind)) {
                    if (*left.type != types_.boolean_type()
                        || *right.type != types_.boolean_type()) {
                        diagnostics_.error(
                            expression.span,
                            "operator '" + operator_name
                                + "' requires two 'bool' operands, got '"
                                + type_name(*left.type) + "' and '"
                                + type_name(*right.type) + "'");
                        return ExpressionResult::error();
                    }
                    return ExpressionResult::value(types_.boolean_type());
                }

                if (is_ordering_operator(node.operator_kind)) {
                    const auto matching_integers =
                        *left.type == types_.integer_type()
                        && *right.type == types_.integer_type();
                    const auto matching_strings =
                        *left.type == types_.string_type()
                        && *right.type == types_.string_type();
                    if (!matching_integers && !matching_strings) {
                        diagnostics_.error(
                            expression.span,
                            "operator '" + operator_name
                                + "' requires two matching 'int' or "
                                  "'string' operands, got '"
                                + type_name(*left.type) + "' and '"
                                + type_name(*right.type) + "'");
                        return ExpressionResult::error();
                    }
                    return ExpressionResult::value(types_.boolean_type());
                }

                if (is_equality_operator(node.operator_kind)) {
                    const auto primitive = primitive_kind(*left.type);
                    if (*left.type != *right.type
                        || !primitive.has_value()
                        || *primitive == PrimitiveTypeKind::void_type) {
                        diagnostics_.error(
                            expression.span,
                            "operator '" + operator_name
                                + "' requires operands of the same scalar "
                                  "type, got '"
                                + type_name(*left.type) + "' and '"
                                + type_name(*right.type) + "'");
                        return ExpressionResult::error();
                    }
                    return ExpressionResult::value(types_.boolean_type());
                }

                diagnostics_.error(
                    expression.span,
                    "malformed AST: unknown binary operator");
                return ExpressionResult::error();
            } else if constexpr (std::is_same_v<Node, CallExpression>) {
                return check_call(expression, node);
            } else if constexpr (std::is_same_v<Node, IndexExpression>) {
                if (node.base == nullptr || node.index == nullptr) {
                    diagnostics_.error(
                        expression.span,
                        "malformed AST: index expression is missing an operand");
                    return ExpressionResult::error();
                }

                const auto base = check_expression(*node.base);
                const auto index = check_expression(*node.index);
                auto index_is_valid = !index.failed();
                if (index.callable.has_value()) {
                    diagnostics_.error(
                        node.index->span,
                        "index must be a value of type 'int'");
                    index_is_valid = false;
                } else if (index.type.has_value()
                    && *index.type != types_.integer_type()) {
                    diagnostics_.error(
                        node.index->span,
                        "index must have type 'int', got '"
                            + type_name(*index.type) + "'");
                    index_is_valid = false;
                }

                if (base.failed() || !index_is_valid) {
                    return ExpressionResult::error();
                }
                if (base.callable.has_value()) {
                    diagnostics_.error(
                        node.base->span,
                        "cannot index a function or member function");
                    return ExpressionResult::error();
                }

                if (*base.type == types_.string_type()) {
                    return ExpressionResult::value(types_.character_type());
                }
                if (const auto descriptor = types_.lookup(*base.type)) {
                    if (const auto* vector =
                            std::get_if<SemanticVectorType>(&*descriptor)) {
                        return ExpressionResult::value(vector->element_type);
                    }
                }

                diagnostics_.error(
                    node.base->span,
                    "cannot index value of type '" + type_name(*base.type)
                        + "'");
                return ExpressionResult::error();
            } else if constexpr (
                std::is_same_v<Node, MemberAccessExpression>) {
                return check_member(expression, node);
            } else if constexpr (
                std::is_same_v<Node, VectorConstructionExpression>) {
                const auto vector_type = type_id_for(types_, node.type);
                if (!vector_type.has_value()) {
                    diagnostics_.error(
                        node.type.span,
                        "malformed AST: invalid vector construction type");
                    for (const auto& argument : node.arguments) {
                        (void)check_required_expression(argument, expression.span);
                    }
                    return ExpressionResult::error();
                }

                const auto descriptor = types_.lookup(*vector_type);
                const auto* vector = descriptor.has_value()
                    ? std::get_if<SemanticVectorType>(&*descriptor)
                    : nullptr;
                if (vector == nullptr) {
                    diagnostics_.error(
                        node.type.span,
                        "malformed AST: vector construction has non-vector type");
                    return ExpressionResult::error();
                }

                auto arguments_are_valid = true;
                std::vector<ExpressionResult> arguments;
                arguments.reserve(node.arguments.size());
                for (const auto& argument : node.arguments) {
                    auto checked =
                        check_required_expression(argument, expression.span);
                    if (checked.failed()) {
                        arguments_are_valid = false;
                    }
                    arguments.push_back(std::move(checked));
                }

                auto construction_is_valid = arguments_are_valid;
                if (node.arguments.size() > 2) {
                    diagnostics_.error(
                        expression.span,
                        "vector construction expects at most 2 arguments, got "
                            + std::to_string(node.arguments.size()));
                    construction_is_valid = false;
                }

                if (!arguments.empty() && !arguments[0].failed()) {
                    if (arguments[0].callable.has_value()
                        || *arguments[0].type != types_.integer_type()) {
                        diagnostics_.error(
                            node.arguments[0]->span,
                            "vector size must have type 'int'");
                        construction_is_valid = false;
                    }
                }
                if (arguments.size() >= 2 && !arguments[1].failed()) {
                    if (arguments[1].callable.has_value()
                        || *arguments[1].type != vector->element_type) {
                        diagnostics_.error(
                            node.arguments[1]->span,
                            "vector initial value must have type '"
                                + type_name(vector->element_type) + "'");
                        construction_is_valid = false;
                    }
                }

                return construction_is_valid
                    ? ExpressionResult::value(*vector_type)
                    : ExpressionResult::error();
            } else if constexpr (
                std::is_same_v<Node, ParenthesizedExpression>) {
                if (node.expression == nullptr) {
                    diagnostics_.error(
                        expression.span,
                        "malformed AST: parenthesized expression is empty");
                    return ExpressionResult::error();
                }
                return check_expression(
                    *node.expression,
                    allow_minimum_integer_magnitude);
            }

            diagnostics_.error(
                expression.span,
                "malformed AST: unknown expression node");
            return ExpressionResult::error();
        },
        expression.node);

    if (result.type.has_value()) {
        type_info_.record(expression, *result.type);
    }
    return result;
}

TypeChecker::ExpressionResult TypeChecker::check_required_expression(
    const ExpressionPtr& expression,
    const SourceSpan owner_span) {
    if (expression == nullptr) {
        diagnostics_.error(owner_span, "malformed AST: missing expression");
        return ExpressionResult::error();
    }
    return check_expression(*expression);
}

TypeChecker::ExpressionResult TypeChecker::check_identifier(
    const Expression& expression,
    const IdentifierExpression& identifier) {
    const auto resolution = resolutions_.resolution_for(identifier);
    if (!resolution.has_value()) {
        diagnostics_.error(
            expression.span,
            "malformed semantic state: identifier has no resolution");
        return ExpressionResult::error();
    }

    if (const auto* builtin =
            std::get_if<BuiltinFunctionKind>(&*resolution)) {
        return ExpressionResult::callable_value(
            Callable{.target = *builtin});
    }

    const auto symbol = std::get<SymbolId>(*resolution);
    const auto& data = symbols_.symbol(symbol).data;
    if (std::holds_alternative<FunctionSymbol>(data)) {
        return ExpressionResult::callable_value(
            Callable{.target = symbol});
    }

    const auto type = type_of_symbol(symbol);
    return type.has_value()
        ? ExpressionResult::value(*type)
        : ExpressionResult::error();
}

TypeChecker::ExpressionResult TypeChecker::check_call(
    const Expression& expression,
    const CallExpression& call) {
    if (call.callee == nullptr) {
        diagnostics_.error(
            expression.span,
            "malformed AST: call expression is missing a callee");
        for (const auto& argument : call.arguments) {
            (void)check_required_expression(argument, expression.span);
        }
        return ExpressionResult::error();
    }

    const auto callee = check_expression(*call.callee);
    if (callee.failed()) {
        for (const auto& argument : call.arguments) {
            (void)check_required_expression(argument, expression.span);
        }
        return ExpressionResult::error();
    }
    if (callee.type.has_value()) {
        for (const auto& argument : call.arguments) {
            (void)check_required_expression(argument, expression.span);
        }
        diagnostics_.error(
            call.callee->span,
            "value of type '" + type_name(*callee.type)
                + "' is not callable");
        return ExpressionResult::error();
    }

    auto receiver_is_valid = true;
    if (const auto* member_kind =
            std::get_if<MemberKind>(&callee.callable->target);
        member_kind != nullptr && *member_kind == MemberKind::string_push) {
        const auto* member = unwrap_member_access(*call.callee);
        if (member == nullptr || member->base == nullptr) {
            diagnostics_.error(
                call.callee->span,
                "malformed semantic state: string member has no receiver");
            receiver_is_valid = false;
        } else if (!is_mutable_string_receiver(*member->base)) {
            diagnostics_.error(
                member->base->span,
                "member function 'push' requires a mutable string receiver");
            receiver_is_valid = false;
        }
    }

    auto result = check_callable(expression, call, *callee.callable);
    return receiver_is_valid ? result : ExpressionResult::error();
}

TypeChecker::ExpressionResult TypeChecker::check_member(
    const Expression& expression,
    const MemberAccessExpression& member) {
    if (member.base == nullptr) {
        diagnostics_.error(
            expression.span,
            "malformed AST: member access is missing a base");
        return ExpressionResult::error();
    }

    const auto base = check_expression(*member.base);
    if (base.failed()) {
        return ExpressionResult::error();
    }
    if (base.callable.has_value()) {
        diagnostics_.error(
            expression.span,
            "function or member function has no member '" + member.member
                + "'");
        return ExpressionResult::error();
    }
    if (*base.type != types_.string_type()) {
        diagnostics_.error(
            expression.span,
            "type '" + type_name(*base.type) + "' has no member '"
                + member.member + "'");
        return ExpressionResult::error();
    }

    auto kind = std::optional<MemberKind>{};
    if (member.member == "length") {
        kind = MemberKind::string_length;
    } else if (member.member == "push") {
        kind = MemberKind::string_push;
    }
    if (!kind.has_value()) {
        diagnostics_.error(
            expression.span,
            "type 'string' has no member '" + member.member + "'");
        return ExpressionResult::error();
    }

    type_info_.record(member, *kind);
    return ExpressionResult::callable_value(Callable{.target = *kind});
}

TypeChecker::ExpressionResult TypeChecker::check_callable(
    const Expression& expression,
    const CallExpression& call,
    const Callable& callable) {
    std::vector<ExpressionResult> arguments;
    arguments.reserve(call.arguments.size());
    auto valid = true;
    for (const auto& argument : call.arguments) {
        auto checked = check_required_expression(argument, expression.span);
        if (checked.failed()) {
            valid = false;
        } else if (checked.callable.has_value()) {
            diagnostics_.error(
                argument != nullptr ? argument->span : expression.span,
                "function argument must be a value");
            valid = false;
        }
        arguments.push_back(std::move(checked));
    }

    if (const auto* user = std::get_if<SymbolId>(&callable.target)) {
        const auto* function =
            std::get_if<FunctionSymbol>(&symbols_.symbol(*user).data);
        if (function == nullptr) {
            diagnostics_.error(
                expression.span,
                "malformed semantic state: callable symbol is not a function");
            return ExpressionResult::error();
        }

        if (call.arguments.size() != function->parameter_types.size()) {
            diagnostics_.error(
                expression.span,
                "function expects "
                    + std::to_string(function->parameter_types.size())
                    + " arguments, got "
                    + std::to_string(call.arguments.size()));
            valid = false;
        }

        const auto compared = std::min(
            arguments.size(),
            function->parameter_types.size());
        for (std::size_t index = 0; index < compared; ++index) {
            if (!arguments[index].type.has_value()) {
                continue;
            }
            const auto expected = function->parameter_types[index];
            if (*arguments[index].type != expected) {
                diagnostics_.error(
                    call.arguments[index]->span,
                    "argument " + std::to_string(index + 1)
                        + " expects '" + type_name(expected) + "', got '"
                        + type_name(*arguments[index].type) + "'");
                valid = false;
            }
        }

        return valid
            ? ExpressionResult::value(function->return_type)
            : ExpressionResult::error();
    }

    if (const auto* builtin =
            std::get_if<BuiltinFunctionKind>(&callable.target)) {
        const auto signature = builtin_function_signature(*builtin);
        if (call.arguments.size() != signature.parameter_types.size()) {
            diagnostics_.error(
                expression.span,
                "function expects "
                    + std::to_string(signature.parameter_types.size())
                    + " arguments, got "
                    + std::to_string(call.arguments.size()));
            valid = false;
        }

        const auto compared = std::min(
            arguments.size(),
            signature.parameter_types.size());
        for (std::size_t index = 0; index < compared; ++index) {
            if (!arguments[index].type.has_value()) {
                continue;
            }
            const auto argument_kind = primitive_kind(*arguments[index].type);
            if (!argument_kind.has_value()
                || !builtin_parameter_accepts(
                    signature.parameter_types[index],
                    *argument_kind)) {
                diagnostics_.error(
                    call.arguments[index]->span,
                    "argument " + std::to_string(index + 1)
                        + " expects '"
                        + parameter_type_name(
                            signature.parameter_types[index])
                        + "', got '"
                        + type_name(*arguments[index].type) + "'");
                valid = false;
            }
        }

        const auto return_type = primitive_type(signature.return_type);
        if (!return_type.has_value()) {
            diagnostics_.error(
                expression.span,
                "malformed semantic state: invalid builtin return type");
            return ExpressionResult::error();
        }
        return valid
            ? ExpressionResult::value(*return_type)
            : ExpressionResult::error();
    }

    const auto member = std::get<MemberKind>(callable.target);
    auto expected_parameters = std::vector<TypeId>{};
    auto return_type = types_.void_type();
    switch (member) {
    case MemberKind::string_length:
        return_type = types_.integer_type();
        break;
    case MemberKind::string_push:
        expected_parameters.push_back(types_.character_type());
        break;
    }

    if (arguments.size() != expected_parameters.size()) {
        diagnostics_.error(
            expression.span,
            "member function expects "
                + std::to_string(expected_parameters.size())
                + " arguments, got " + std::to_string(arguments.size()));
        valid = false;
    }

    const auto compared =
        std::min(arguments.size(), expected_parameters.size());
    for (std::size_t index = 0; index < compared; ++index) {
        if (!arguments[index].type.has_value()) {
            continue;
        }
        if (*arguments[index].type != expected_parameters[index]) {
            diagnostics_.error(
                call.arguments[index]->span,
                "argument " + std::to_string(index + 1)
                    + " expects '" + type_name(expected_parameters[index])
                    + "', got '" + type_name(*arguments[index].type) + "'");
            valid = false;
        }
    }

    return valid
        ? ExpressionResult::value(return_type)
        : ExpressionResult::error();
}

std::optional<TypeId> TypeChecker::check_assignment_target(
    const AssignmentTarget& target) {
    const auto resolution = resolutions_.resolution_for(target);
    if (!resolution.has_value()) {
        diagnostics_.error(
            target.name_span,
            "malformed semantic state: assignment target has no resolution");
        for (const auto& index : target.indices) {
            (void)check_required_expression(index, target.span);
        }
        return std::nullopt;
    }

    auto current_type = std::optional<TypeId>{};
    if (const auto* builtin =
            std::get_if<BuiltinFunctionKind>(&*resolution)) {
        (void)builtin;
        diagnostics_.error(
            target.name_span,
            "builtin function cannot be assigned to");
    } else {
        const auto symbol = std::get<SymbolId>(*resolution);
        if (std::holds_alternative<FunctionSymbol>(
                symbols_.symbol(symbol).data)) {
            diagnostics_.error(
                target.name_span,
                "function cannot be assigned to");
        } else {
            current_type = type_of_symbol(symbol);
        }
    }

    auto valid = current_type.has_value();
    for (const auto& index : target.indices) {
        const auto checked = check_required_expression(index, target.span);
        if (checked.failed()) {
            valid = false;
            continue;
        }
        if (checked.callable.has_value()
            || *checked.type != types_.integer_type()) {
            diagnostics_.error(
                index != nullptr ? index->span : target.span,
                "index must have type 'int'");
            valid = false;
            continue;
        }
        if (!current_type.has_value()) {
            continue;
        }

        if (*current_type == types_.string_type()) {
            current_type = types_.character_type();
            continue;
        }
        const auto descriptor = types_.lookup(*current_type);
        const auto* vector = descriptor.has_value()
            ? std::get_if<SemanticVectorType>(&*descriptor)
            : nullptr;
        if (vector == nullptr) {
            diagnostics_.error(
                target.span,
                "cannot index assignment target of type '"
                    + type_name(*current_type) + "'");
            current_type.reset();
            valid = false;
            continue;
        }
        current_type = vector->element_type;
    }

    if (!valid || !current_type.has_value()) {
        return std::nullopt;
    }

    type_info_.record(target, *current_type);
    return current_type;
}

bool TypeChecker::is_mutable_string_receiver(
    const Expression& expression) const {
    return std::visit(
        [this](const auto& node) -> bool {
            using Node = std::decay_t<decltype(node)>;

            if constexpr (std::is_same_v<Node, IdentifierExpression>) {
                const auto resolution = resolutions_.resolution_for(node);
                if (!resolution.has_value()) {
                    return false;
                }

                const auto* symbol = std::get_if<SymbolId>(&*resolution);
                if (symbol == nullptr) {
                    return false;
                }

                const auto& data = symbols_.symbol(*symbol).data;
                return std::holds_alternative<VariableSymbol>(data)
                    || std::holds_alternative<ParameterSymbol>(data);
            } else if constexpr (
                std::is_same_v<Node, ParenthesizedExpression>) {
                return node.expression != nullptr
                    && is_mutable_string_receiver(*node.expression);
            } else if constexpr (std::is_same_v<Node, IndexExpression>) {
                return node.base != nullptr
                    && is_mutable_string_receiver(*node.base);
            } else {
                return false;
            }
        },
        expression.node);
}

void TypeChecker::check_condition(
    const ExpressionPtr& condition,
    const SourceSpan owner_span) {
    const auto checked = check_required_expression(condition, owner_span);
    if (checked.failed()) {
        return;
    }
    const auto span = condition != nullptr ? condition->span : owner_span;
    if (checked.callable.has_value()) {
        diagnostics_.error(span, "condition must be a value of type 'bool'");
    } else if (*checked.type != types_.boolean_type()) {
        diagnostics_.error(
            span,
            "condition must have type 'bool', got '"
                + type_name(*checked.type) + "'");
    }
}

void TypeChecker::check_child_block(
    const BlockPtr& block,
    const SourceSpan owner_span) {
    if (block == nullptr) {
        diagnostics_.error(owner_span, "malformed AST: missing block");
        return;
    }
    check_block(*block);
}

void TypeChecker::report_type_mismatch(
    const SourceSpan span,
    std::string context,
    const TypeId expected,
    const TypeId actual) {
    diagnostics_.error(
        span,
        std::move(context) + " value of type '" + type_name(actual)
            + "'; expected '" + type_name(expected) + "'");
}

std::optional<TypeId> TypeChecker::type_of_symbol(
    const SymbolId symbol) const {
    const auto& data = symbols_.symbol(symbol).data;
    if (const auto* variable = std::get_if<VariableSymbol>(&data)) {
        return variable->type.has_value()
            ? variable->type
            : type_info_.inferred_type(symbol);
    }
    if (const auto* parameter = std::get_if<ParameterSymbol>(&data)) {
        return parameter->type;
    }
    return std::nullopt;
}

std::optional<PrimitiveTypeKind> TypeChecker::primitive_kind(
    const TypeId type) const {
    const auto descriptor = types_.lookup(type);
    if (!descriptor.has_value()) {
        return std::nullopt;
    }
    if (const auto* primitive =
            std::get_if<PrimitiveTypeKind>(&*descriptor)) {
        return *primitive;
    }
    return std::nullopt;
}

std::optional<TypeId> TypeChecker::primitive_type(
    const PrimitiveTypeKind kind) const noexcept {
    switch (kind) {
    case PrimitiveTypeKind::integer:
        return types_.integer_type();
    case PrimitiveTypeKind::boolean:
        return types_.boolean_type();
    case PrimitiveTypeKind::character:
        return types_.character_type();
    case PrimitiveTypeKind::string:
        return types_.string_type();
    case PrimitiveTypeKind::void_type:
        return types_.void_type();
    }
    return std::nullopt;
}

std::string TypeChecker::type_name(const TypeId type) const {
    const auto descriptor = types_.lookup(type);
    if (!descriptor.has_value()) {
        return "<invalid>";
    }

    if (const auto* primitive =
            std::get_if<PrimitiveTypeKind>(&*descriptor)) {
        switch (*primitive) {
        case PrimitiveTypeKind::integer:
            return "int";
        case PrimitiveTypeKind::boolean:
            return "bool";
        case PrimitiveTypeKind::character:
            return "char";
        case PrimitiveTypeKind::string:
            return "string";
        case PrimitiveTypeKind::void_type:
            return "void";
        }
    }

    const auto& vector = std::get<SemanticVectorType>(*descriptor);
    return "vector<" + type_name(vector.element_type) + ">";
}

}

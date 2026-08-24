#include "pseudo/semantic/name_resolver.hpp"

#include "pseudo/diagnostics/diagnostic_engine.hpp"
#include "pseudo/semantic/builtin.hpp"

#include <cstddef>
#include <string>
#include <type_traits>
#include <variant>

namespace tpp {

NameResolver::NameResolver(
    const SymbolTable& symbols,
    const DeclarationInfo& declarations,
    ResolutionInfo& resolutions,
    DiagnosticEngine& diagnostics)
    : symbols_{symbols},
      declarations_{declarations},
      resolutions_{resolutions},
      diagnostics_{diagnostics} {}

bool NameResolver::resolve(const Program& program) {
    const auto initial_error_count = diagnostics_.error_count();
    active_local_variables_.assign(symbols_.symbol_count(), false);

    for (const auto& declaration : program.declarations) {
        resolve_top_level(declaration);
    }

    return diagnostics_.error_count() == initial_error_count;
}

void NameResolver::resolve_top_level(
    const TopLevelDeclaration& declaration) {
    std::visit(
        [this](const auto& node) {
            using Node = std::decay_t<decltype(node)>;

            if constexpr (std::is_same_v<Node, FunctionDeclaration>) {
                resolve_function(node);
            } else {
                resolve_variable(
                    node,
                    symbols_.global_scope(),
                    std::nullopt);
            }
        },
        declaration);
}

void NameResolver::resolve_function(
    const FunctionDeclaration& declaration) {
    if (declaration.body == nullptr) {
        diagnostics_.error(
            declaration.span,
            "name resolution encountered a function without a body");
        return;
    }

    const auto function_scope = declarations_.scope_for(*declaration.body);
    if (!function_scope.has_value()) {
        diagnostics_.error(
            declaration.body->span,
            "missing scope information during name resolution");
        return;
    }

    resolve_block(
        *declaration.body,
        *function_scope,
        *function_scope);
}

void NameResolver::resolve_block(
    const Block& block,
    const ScopeId scope,
    const ScopeId function_scope) {
    for (const auto& item : block.items) {
        resolve_block_item(item, scope, function_scope);
    }
}

void NameResolver::resolve_block_item(
    const BlockItem& item,
    const ScopeId scope,
    const ScopeId function_scope) {
    std::visit(
        [this, scope, function_scope](const auto& node) {
            using Node = std::decay_t<decltype(node)>;

            if constexpr (std::is_same_v<Node, FunctionDeclaration>) {
                resolve_function(node);
            } else {
                resolve_statement(node, scope, function_scope);
            }
        },
        item);
}

void NameResolver::resolve_statement(
    const Statement& statement,
    const ScopeId scope,
    const ScopeId function_scope) {
    std::visit(
        [this, span = statement.span, scope, function_scope](const auto& node) {
            using Node = std::decay_t<decltype(node)>;

            if constexpr (std::is_same_v<Node, VariableDeclaration>) {
                resolve_variable(node, scope, function_scope);
            } else if constexpr (std::is_same_v<Node, AssignmentStatement>) {
                resolve_assignment_target(
                    node.target,
                    scope,
                    function_scope);
                resolve_required_expression(
                    node.value,
                    span,
                    scope,
                    function_scope);
            } else if constexpr (std::is_same_v<Node, ExpressionStatement>) {
                resolve_required_expression(
                    node.expression,
                    span,
                    scope,
                    function_scope);
            } else if constexpr (std::is_same_v<Node, IfStatement>) {
                resolve_required_expression(
                    node.condition,
                    span,
                    scope,
                    function_scope);
                resolve_child_block(node.then_block, span, function_scope);
                if (node.else_block != nullptr) {
                    resolve_child_block(
                        node.else_block,
                        span,
                        function_scope);
                }
            } else if constexpr (std::is_same_v<Node, WhileStatement>) {
                resolve_required_expression(
                    node.condition,
                    span,
                    scope,
                    function_scope);
                resolve_child_block(node.body, span, function_scope);
            } else if constexpr (std::is_same_v<Node, ForRangeStatement>) {
                resolve_for_range(node, span, scope, function_scope);
            } else if constexpr (std::is_same_v<Node, ForEachStatement>) {
                resolve_for_each(node, span, scope, function_scope);
            } else if constexpr (std::is_same_v<Node, ReturnStatement>) {
                if (node.value != nullptr) {
                    resolve_expression(
                        *node.value,
                        scope,
                        function_scope);
                }
            } else if constexpr (std::is_same_v<Node, BlockStatement>) {
                resolve_child_block(node.block, span, function_scope);
            }
        },
        statement.node);
}

void NameResolver::resolve_variable(
    const VariableDeclaration& declaration,
    const ScopeId scope,
    const std::optional<ScopeId> function_scope) {
    if (declaration.initializer != nullptr) {
        resolve_expression(
            *declaration.initializer,
            scope,
            function_scope);
    }

    if (scope == symbols_.global_scope()) {
        return;
    }

    const auto symbol = declarations_.symbol_for(declaration);
    if (!symbol.has_value()) {
        diagnostics_.error(
            declaration.name_span,
            "missing declaration information during name resolution");
        return;
    }

    activate(*symbol);
}

void NameResolver::resolve_assignment_target(
    const AssignmentTarget& target,
    const ScopeId scope,
    const std::optional<ScopeId> function_scope) {
    auto resolution =
        resolve_name(target.name, target.name_span, scope, function_scope);
    if (resolution.has_value()) {
        resolutions_.record(target, *resolution);
    }

    for (const auto& index : target.indices) {
        resolve_required_expression(
            index,
            target.span,
            scope,
            function_scope);
    }
}

void NameResolver::resolve_expression(
    const Expression& expression,
    const ScopeId scope,
    const std::optional<ScopeId> function_scope) {
    std::visit(
        [this, span = expression.span, scope, function_scope](
            const auto& node) {
            using Node = std::decay_t<decltype(node)>;

            if constexpr (std::is_same_v<Node, IdentifierExpression>) {
                auto resolution =
                    resolve_name(node.name, span, scope, function_scope);
                if (resolution.has_value()) {
                    resolutions_.record(node, *resolution);
                }
            } else if constexpr (std::is_same_v<Node, UnaryExpression>) {
                resolve_required_expression(
                    node.operand,
                    span,
                    scope,
                    function_scope);
            } else if constexpr (std::is_same_v<Node, BinaryExpression>) {
                resolve_required_expression(
                    node.left,
                    span,
                    scope,
                    function_scope);
                resolve_required_expression(
                    node.right,
                    span,
                    scope,
                    function_scope);
            } else if constexpr (std::is_same_v<Node, CallExpression>) {
                resolve_required_expression(
                    node.callee,
                    span,
                    scope,
                    function_scope);
                for (const auto& argument : node.arguments) {
                    resolve_required_expression(
                        argument,
                        span,
                        scope,
                        function_scope);
                }
            } else if constexpr (std::is_same_v<Node, IndexExpression>) {
                resolve_required_expression(
                    node.base,
                    span,
                    scope,
                    function_scope);
                resolve_required_expression(
                    node.index,
                    span,
                    scope,
                    function_scope);
            } else if constexpr (std::is_same_v<Node, MemberAccessExpression>) {
                resolve_required_expression(
                    node.base,
                    span,
                    scope,
                    function_scope);
            } else if constexpr (
                std::is_same_v<Node, VectorConstructionExpression>) {
                for (const auto& argument : node.arguments) {
                    resolve_required_expression(
                        argument,
                        span,
                        scope,
                        function_scope);
                }
            } else if constexpr (
                std::is_same_v<Node, ParenthesizedExpression>) {
                resolve_required_expression(
                    node.expression,
                    span,
                    scope,
                    function_scope);
            }
        },
        expression.node);
}

void NameResolver::resolve_required_expression(
    const ExpressionPtr& expression,
    const SourceSpan owner_span,
    const ScopeId scope,
    const std::optional<ScopeId> function_scope) {
    if (expression == nullptr) {
        diagnostics_.error(
            owner_span,
            "name resolution encountered a missing expression");
        return;
    }

    resolve_expression(*expression, scope, function_scope);
}

void NameResolver::resolve_child_block(
    const BlockPtr& block,
    const SourceSpan owner_span,
    const ScopeId function_scope) {
    if (block == nullptr) {
        diagnostics_.error(
            owner_span,
            "name resolution encountered a missing block");
        return;
    }

    const auto scope = declarations_.scope_for(*block);
    if (!scope.has_value()) {
        diagnostics_.error(
            block->span,
            "missing scope information during name resolution");
        return;
    }

    resolve_block(*block, *scope, function_scope);
}

void NameResolver::resolve_for_range(
    const ForRangeStatement& statement,
    const SourceSpan owner_span,
    const ScopeId scope,
    const ScopeId function_scope) {
    resolve_required_expression(
        statement.begin,
        owner_span,
        scope,
        function_scope);
    resolve_required_expression(
        statement.end,
        owner_span,
        scope,
        function_scope);

    const auto binding = declarations_.symbol_for(statement);
    if (binding.has_value()) {
        activate(*binding);
    } else {
        diagnostics_.error(
            statement.variable_span,
            "missing declaration information during name resolution");
    }

    resolve_child_block(statement.body, owner_span, function_scope);
}

void NameResolver::resolve_for_each(
    const ForEachStatement& statement,
    const SourceSpan owner_span,
    const ScopeId scope,
    const ScopeId function_scope) {
    resolve_required_expression(
        statement.iterable,
        owner_span,
        scope,
        function_scope);

    const auto binding = declarations_.symbol_for(statement);
    if (binding.has_value()) {
        activate(*binding);
    } else {
        diagnostics_.error(
            statement.variable_span,
            "missing declaration information during name resolution");
    }

    resolve_child_block(statement.body, owner_span, function_scope);
}

std::optional<ResolutionTarget> NameResolver::resolve_name(
    const std::string_view name,
    const SourceSpan span,
    const ScopeId scope,
    const std::optional<ScopeId> function_scope) {
    auto current = std::optional<ScopeId>{scope};
    auto later_local = std::optional<SymbolId>{};
    auto crossed_function_boundary = false;

    while (current.has_value()) {
        const auto symbol = symbols_.lookup_local(*current, name);
        if (symbol.has_value()) {
            if (is_visible(*symbol, *current)) {
                const auto& declaration = symbols_.symbol(*symbol);
                const auto is_outer_local =
                    crossed_function_boundary
                    && *current != symbols_.global_scope()
                    && (std::holds_alternative<VariableSymbol>(
                            declaration.data)
                        || std::holds_alternative<ParameterSymbol>(
                            declaration.data));

                if (is_outer_local) {
                    diagnostics_.error(
                        span,
                        "nested function cannot capture '"
                            + std::string{name}
                            + "' from an enclosing function");
                    diagnostics_.note(
                        declaration.declaration_span,
                        "declaration is here");
                }

                return ResolutionTarget{*symbol};
            }

            if (!later_local.has_value()) {
                later_local = *symbol;
            }
        }

        if (function_scope.has_value() && *current == *function_scope) {
            crossed_function_boundary = true;
        }

        current = symbols_.scope(*current).parent();
    }

    if (const auto builtin = find_builtin_function(name)) {
        return ResolutionTarget{*builtin};
    }

    if (later_local.has_value()) {
        diagnostics_.error(
            span,
            "name '" + std::string{name}
                + "' is used before its declaration");
        diagnostics_.note(
            symbols_.symbol(*later_local).declaration_span,
            "declaration is here");
        return std::nullopt;
    }

    diagnostics_.error(
        span,
        "unknown name '" + std::string{name} + "'");
    return std::nullopt;
}

bool NameResolver::is_visible(
    const SymbolId symbol,
    const ScopeId declaring_scope) const {
    const auto& declaration = symbols_.symbol(symbol);
    if (!std::holds_alternative<VariableSymbol>(declaration.data)) {
        return true;
    }

    if (declaring_scope == symbols_.global_scope()) {
        return true;
    }

    return symbol.value < active_local_variables_.size()
        && active_local_variables_[symbol.value];
}

void NameResolver::activate(const SymbolId symbol) {
    if (symbol.value >= active_local_variables_.size()) {
        return;
    }

    active_local_variables_[symbol.value] = true;
}

}

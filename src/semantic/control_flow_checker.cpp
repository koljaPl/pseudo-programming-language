#include "pseudo/semantic/control_flow_checker.hpp"

#include "pseudo/diagnostics/diagnostic_engine.hpp"

#include <string>
#include <type_traits>
#include <variant>

namespace tpp {
namespace {

[[nodiscard]] bool is_void_return_type(
    const ReturnType& return_type) noexcept {
    return std::holds_alternative<VoidType>(return_type.node);
}

[[nodiscard]] bool is_integer_return_type(
    const ReturnType& return_type) noexcept {
    const auto* value_type = std::get_if<ValueType>(&return_type.node);
    if (value_type == nullptr) {
        return false;
    }

    const auto* scalar =
        std::get_if<ScalarTypeKind>(&value_type->node);
    return scalar != nullptr && *scalar == ScalarTypeKind::integer;
}

[[nodiscard]] bool is_main_exemption(
    const FunctionDeclaration& declaration,
    const bool is_top_level) noexcept {
    return is_top_level
        && declaration.name == "main"
        && declaration.parameters.empty()
        && is_integer_return_type(declaration.return_type);
}

}

struct ControlFlowChecker::FlowSummary {
    bool falls_through = true;
    bool returns = false;
    bool breaks = false;
    bool continues = false;
    bool malformed = false;
};

ControlFlowChecker::ControlFlowChecker(DiagnosticEngine& diagnostics)
    : diagnostics_{diagnostics} {}

bool ControlFlowChecker::check(const Program& program) {
    const auto initial_error_count = diagnostics_.error_count();

    for (const auto& declaration : program.declarations) {
        if (const auto* function =
                std::get_if<FunctionDeclaration>(&declaration)) {
            check_function(*function, true);
        }
    }

    return diagnostics_.error_count() == initial_error_count;
}

void ControlFlowChecker::check_function(
    const FunctionDeclaration& declaration,
    const bool is_top_level) {
    if (declaration.body == nullptr) {
        diagnostics_.error(
            declaration.span,
            "malformed AST: function is missing a body");
        return;
    }

    const auto flow = check_block(
        *declaration.body,
        0,
        true);
    if (!flow.malformed
        && flow.falls_through
        && !is_void_return_type(declaration.return_type)
        && !is_main_exemption(declaration, is_top_level)) {
        diagnostics_.error(
            declaration.name_span,
            "non-void function '" + declaration.name
                + "' may reach the end without returning a value");
    }
}

ControlFlowChecker::FlowSummary ControlFlowChecker::check_block(
    const Block& block,
    const std::size_t loop_depth,
    const bool report_unreachable) {
    auto result = FlowSummary{};
    auto reachable = true;
    auto reported_unreachable_suffix = false;

    for (const auto& item : block.items) {
        if (const auto* function =
                std::get_if<FunctionDeclaration>(&item)) {
            // A nested declaration has no runtime effect in its enclosing
            // block, but its body is an independent function context.
            check_function(*function, false);
            continue;
        }

        const auto& statement = std::get<Statement>(item);
        if (!reachable) {
            if (report_unreachable && !reported_unreachable_suffix) {
                diagnostics_.warning(
                    statement.span,
                    "unreachable statement");
                reported_unreachable_suffix = true;
            }

            // Still validate illegal transfers and malformed descendants, but
            // avoid a warning cascade below an already unreachable statement.
            const auto unreachable = check_statement(
                statement,
                loop_depth,
                false);
            result.malformed = result.malformed || unreachable.malformed;
            continue;
        }

        const auto statement_flow = check_statement(
            statement,
            loop_depth,
            report_unreachable);
        result.returns = result.returns || statement_flow.returns;
        result.breaks = result.breaks || statement_flow.breaks;
        result.continues = result.continues || statement_flow.continues;
        result.malformed = result.malformed || statement_flow.malformed;
        reachable = statement_flow.falls_through;
    }

    result.falls_through = reachable;
    return result;
}

ControlFlowChecker::FlowSummary ControlFlowChecker::check_statement(
    const Statement& statement,
    const std::size_t loop_depth,
    const bool report_unreachable) {
    return std::visit(
        [this, &statement, loop_depth, report_unreachable](
            const auto& node) -> FlowSummary {
            using Node = std::decay_t<decltype(node)>;

            if constexpr (
                std::is_same_v<Node, VariableDeclaration>
                || std::is_same_v<Node, AssignmentStatement>
                || std::is_same_v<Node, ExpressionStatement>) {
                return {};
            } else if constexpr (std::is_same_v<Node, IfStatement>) {
                auto then_flow = check_required_block(
                    node.then_block,
                    statement.span,
                    "malformed AST: if statement is missing a then block",
                    loop_depth,
                    report_unreachable);
                auto else_flow = FlowSummary{};
                if (node.else_block != nullptr) {
                    else_flow = check_block(
                        *node.else_block,
                        loop_depth,
                        report_unreachable);
                }

                return FlowSummary{
                    .falls_through =
                        then_flow.falls_through || else_flow.falls_through,
                    .returns = then_flow.returns || else_flow.returns,
                    .breaks = then_flow.breaks || else_flow.breaks,
                    .continues =
                        then_flow.continues || else_flow.continues,
                    .malformed =
                        then_flow.malformed || else_flow.malformed,
                };
            } else if constexpr (std::is_same_v<Node, WhileStatement>) {
                const auto body_flow = check_required_block(
                    node.body,
                    statement.span,
                    "malformed AST: while statement is missing a body",
                    loop_depth + 1,
                    report_unreachable);
                return FlowSummary{
                    // Even a literal-true loop is conservative at this stage.
                    .falls_through = true,
                    .returns = body_flow.returns,
                    .breaks = false,
                    .continues = false,
                    .malformed = body_flow.malformed,
                };
            } else if constexpr (std::is_same_v<Node, ForRangeStatement>) {
                const auto body_flow = check_required_block(
                    node.body,
                    statement.span,
                    "malformed AST: for-range statement is missing a body",
                    loop_depth + 1,
                    report_unreachable);
                return FlowSummary{
                    .falls_through = true,
                    .returns = body_flow.returns,
                    .breaks = false,
                    .continues = false,
                    .malformed = body_flow.malformed,
                };
            } else if constexpr (std::is_same_v<Node, ForEachStatement>) {
                const auto body_flow = check_required_block(
                    node.body,
                    statement.span,
                    "malformed AST: for-each statement is missing a body",
                    loop_depth + 1,
                    report_unreachable);
                return FlowSummary{
                    .falls_through = true,
                    .returns = body_flow.returns,
                    .breaks = false,
                    .continues = false,
                    .malformed = body_flow.malformed,
                };
            } else if constexpr (std::is_same_v<Node, ReturnStatement>) {
                return FlowSummary{
                    .falls_through = false,
                    .returns = true,
                };
            } else if constexpr (std::is_same_v<Node, BreakStatement>) {
                if (loop_depth == 0) {
                    diagnostics_.error(
                        statement.span,
                        "'break' is only allowed inside a loop");
                    return {};
                }
                return FlowSummary{
                    .falls_through = false,
                    .breaks = true,
                };
            } else if constexpr (std::is_same_v<Node, ContinueStatement>) {
                if (loop_depth == 0) {
                    diagnostics_.error(
                        statement.span,
                        "'continue' is only allowed inside a loop");
                    return {};
                }
                return FlowSummary{
                    .falls_through = false,
                    .continues = true,
                };
            } else if constexpr (std::is_same_v<Node, BlockStatement>) {
                return check_required_block(
                    node.block,
                    statement.span,
                    "malformed AST: block statement is missing a block",
                    loop_depth,
                    report_unreachable);
            }

            return FlowSummary{.malformed = true};
        },
        statement.node);
}

ControlFlowChecker::FlowSummary ControlFlowChecker::check_required_block(
    const BlockPtr& block,
    const SourceSpan owner_span,
    const char* const missing_message,
    const std::size_t loop_depth,
    const bool report_unreachable) {
    if (block == nullptr) {
        diagnostics_.error(owner_span, missing_message);
        return FlowSummary{
            .falls_through = true,
            .malformed = true,
        };
    }

    return check_block(*block, loop_depth, report_unreachable);
}

}

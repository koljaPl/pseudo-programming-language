#pragma once

#include "pseudo/ast/program.hpp"

#include <cstddef>

namespace tpp {

class DiagnosticEngine;

// Control-flow checking validates a Program that has successfully completed
// type checking. It does not mutate the AST or retain node addresses.
class ControlFlowChecker {
public:
    explicit ControlFlowChecker(DiagnosticEngine& diagnostics);

    [[nodiscard]] bool check(const Program& program);

private:
    struct FlowSummary;

    void check_function(
        const FunctionDeclaration& declaration,
        bool is_top_level);
    [[nodiscard]] FlowSummary check_block(
        const Block& block,
        std::size_t loop_depth,
        bool report_unreachable);
    [[nodiscard]] FlowSummary check_statement(
        const Statement& statement,
        std::size_t loop_depth,
        bool report_unreachable);

    [[nodiscard]] FlowSummary check_required_block(
        const BlockPtr& block,
        SourceSpan owner_span,
        const char* missing_message,
        std::size_t loop_depth,
        bool report_unreachable);

    DiagnosticEngine& diagnostics_;
};

}

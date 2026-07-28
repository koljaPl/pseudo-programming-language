#pragma once

#include "pseudo/diagnostics/diagnostic_engine.hpp"
#include "pseudo/lexer/token.hpp"
#include "pseudo/source/source_manager.hpp"

#include <vector>

namespace tpp {

class CompilationSession {
public:
    [[nodiscard]] SourceManager& sources() noexcept {
        return sources_;
    }

    [[nodiscard]] const SourceManager& sources() const noexcept {
        return sources_;
    }

    [[nodiscard]] DiagnosticEngine& diagnostics() noexcept {
        return diagnostics_;
    }

    [[nodiscard]] const DiagnosticEngine& diagnostics() const noexcept {
        return diagnostics_;
    }

    [[nodiscard]] std::vector<Token>& tokens() noexcept {
        return tokens_;
    }

    [[nodiscard]] const std::vector<Token>& tokens() const noexcept {
        return tokens_;
    }

private:
    SourceManager sources_;
    DiagnosticEngine diagnostics_;
    std::vector<Token> tokens_;
};

}

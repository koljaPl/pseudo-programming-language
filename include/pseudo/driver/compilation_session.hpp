#pragma once

#include "pseudo/ast/program.hpp"
#include "pseudo/diagnostics/diagnostic_engine.hpp"
#include "pseudo/lexer/token.hpp"
#include "pseudo/semantic/symbol_table.hpp"
#include "pseudo/semantic/type_context.hpp"
#include "pseudo/source/source_manager.hpp"

#include <optional>
#include <vector>

namespace tpp {

class CompilationSession {
public:
    void reset() {
        sources_ = SourceManager{};
        diagnostics_ = DiagnosticEngine{};
        symbols_ = SymbolTable{};
        types_ = TypeContext{};
        tokens_.clear();
        program_.reset();
    }

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

    [[nodiscard]] TypeContext& types() noexcept {
        return types_;
    }

    [[nodiscard]] const TypeContext& types() const noexcept {
        return types_;
    }

    [[nodiscard]] SymbolTable& symbols() noexcept {
        return symbols_;
    }

    [[nodiscard]] const SymbolTable& symbols() const noexcept {
        return symbols_;
    }

    [[nodiscard]] std::vector<Token>& tokens() noexcept {
        return tokens_;
    }

    [[nodiscard]] const std::vector<Token>& tokens() const noexcept {
        return tokens_;
    }

    [[nodiscard]] std::optional<Program>& program() noexcept {
        return program_;
    }

    [[nodiscard]] const std::optional<Program>& program() const noexcept {
        return program_;
    }

private:
    SourceManager sources_;
    DiagnosticEngine diagnostics_;
    TypeContext types_;
    SymbolTable symbols_;
    std::vector<Token> tokens_;
    std::optional<Program> program_;
};

}

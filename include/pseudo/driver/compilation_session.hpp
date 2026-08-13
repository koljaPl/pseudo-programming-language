#pragma once

#include "pseudo/ast/program.hpp"
#include "pseudo/diagnostics/diagnostic_engine.hpp"
#include "pseudo/lexer/token.hpp"
#include "pseudo/semantic/declaration_info.hpp"
#include "pseudo/semantic/resolution_info.hpp"
#include "pseudo/semantic/symbol_table.hpp"
#include "pseudo/semantic/type_context.hpp"
#include "pseudo/semantic/type_info.hpp"
#include "pseudo/source/source_manager.hpp"

#include <optional>
#include <vector>

namespace tpp {

class CompilationSession {
public:
    void reset() {
        type_info_ = TypeInfo{};
        resolutions_ = ResolutionInfo{};
        declarations_ = DeclarationInfo{};
        program_.reset();
        tokens_.clear();
        symbols_ = SymbolTable{};
        types_ = TypeContext{};
        diagnostics_ = DiagnosticEngine{};
        sources_ = SourceManager{};
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

    [[nodiscard]] DeclarationInfo& declarations() noexcept {
        return declarations_;
    }

    [[nodiscard]] const DeclarationInfo& declarations() const noexcept {
        return declarations_;
    }

    [[nodiscard]] ResolutionInfo& resolutions() noexcept {
        return resolutions_;
    }

    [[nodiscard]] const ResolutionInfo& resolutions() const noexcept {
        return resolutions_;
    }

    [[nodiscard]] TypeInfo& type_info() noexcept {
        return type_info_;
    }

    [[nodiscard]] const TypeInfo& type_info() const noexcept {
        return type_info_;
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
    DeclarationInfo declarations_;
    ResolutionInfo resolutions_;
    TypeInfo type_info_;
};

}

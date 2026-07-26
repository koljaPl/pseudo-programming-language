#pragma once

#include "pseudo/diagnostics/diagnostic_engine.hpp"
#include "pseudo/source/source_manager.hpp"

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

private:
    SourceManager sources_;
    DiagnosticEngine diagnostics_;
};

}

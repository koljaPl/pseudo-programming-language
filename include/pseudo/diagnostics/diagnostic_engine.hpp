#pragma once

#include "pseudo/diagnostics/diagnostic.hpp"

#include <cstddef>
#include <iosfwd>
#include <span>
#include <string>
#include <vector>

namespace tpp {

class SourceManager;

class DiagnosticEngine {
public:
    void report(Diagnostic diagnostic);

    void note(std::string message);
    void note(SourceSpan span, std::string message);
    void warning(std::string message);
    void warning(SourceSpan span, std::string message);
    void error(std::string message);
    void error(SourceSpan span, std::string message);

    [[nodiscard]] bool has_errors() const noexcept;
    [[nodiscard]] std::size_t error_count() const noexcept;
    [[nodiscard]] std::span<const Diagnostic> diagnostics() const noexcept;

private:
    std::vector<Diagnostic> diagnostics_;
    std::size_t error_count_ = 0;
};

void render_diagnostics(
    std::ostream& output,
    std::span<const Diagnostic> diagnostics,
    const SourceManager& sources);

}

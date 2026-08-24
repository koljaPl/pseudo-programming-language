#include "pseudo/diagnostics/diagnostic_engine.hpp"

#include "pseudo/source/source_manager.hpp"

#include <algorithm>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>

namespace tpp {
namespace {

std::string_view severity_name(DiagnosticSeverity severity) {
    switch (severity) {
    case DiagnosticSeverity::note:
        return "note";
    case DiagnosticSeverity::warning:
        return "warning";
    case DiagnosticSeverity::error:
        return "error";
    }

    return "unknown";
}

void render_source_line(
    std::ostream& output,
    const Diagnostic& diagnostic,
    const SourceManager& sources) {
    const auto span = *diagnostic.primary_span;
    const auto selected_source = sources.slice(span);
    const SourceLocation location{
        .source = span.source,
        .offset = span.begin,
    };
    const auto position = sources.position(location);
    const auto source_line = sources.line_containing(location);
    const auto line_number = std::to_string(position.line);

    output << sources.display_name(span.source) << ':' << position.line << ':'
           << position.column << ": " << severity_name(diagnostic.severity)
           << ": " << diagnostic.message << '\n';
    output << "  " << line_number << " | " << source_line << '\n';
    output << "  " << std::string(line_number.size(), ' ') << " | ";

    const auto prefix_length = position.column - 1;
    for (std::size_t index = 0; index < prefix_length; ++index) {
        output << (index < source_line.size() && source_line[index] == '\t'
                       ? '\t'
                       : ' ');
    }

    const auto available =
        prefix_length < source_line.size()
            ? source_line.size() - prefix_length
            : std::size_t{0};
    const auto requested = selected_source.size();
    const auto marker_length =
        requested == 0 ? std::size_t{1}
                       : std::max(std::size_t{1}, std::min(requested, available));

    output << '^' << std::string(marker_length - 1, '~') << '\n';
}

}

void DiagnosticEngine::report(Diagnostic diagnostic) {
    if (diagnostic.severity == DiagnosticSeverity::error) {
        ++error_count_;
    }

    diagnostics_.push_back(std::move(diagnostic));
}

void DiagnosticEngine::note(std::string message) {
    report(Diagnostic{
        .severity = DiagnosticSeverity::note,
        .message = std::move(message),
        .primary_span = std::nullopt,
    });
}

void DiagnosticEngine::note(SourceSpan span, std::string message) {
    report(Diagnostic{
        .severity = DiagnosticSeverity::note,
        .message = std::move(message),
        .primary_span = span,
    });
}

void DiagnosticEngine::warning(std::string message) {
    report(Diagnostic{
        .severity = DiagnosticSeverity::warning,
        .message = std::move(message),
        .primary_span = std::nullopt,
    });
}

void DiagnosticEngine::warning(SourceSpan span, std::string message) {
    report(Diagnostic{
        .severity = DiagnosticSeverity::warning,
        .message = std::move(message),
        .primary_span = span,
    });
}

void DiagnosticEngine::error(std::string message) {
    report(Diagnostic{
        .severity = DiagnosticSeverity::error,
        .message = std::move(message),
        .primary_span = std::nullopt,
    });
}

void DiagnosticEngine::error(SourceSpan span, std::string message) {
    report(Diagnostic{
        .severity = DiagnosticSeverity::error,
        .message = std::move(message),
        .primary_span = span,
    });
}

bool DiagnosticEngine::has_errors() const noexcept {
    return error_count_ != 0;
}

std::size_t DiagnosticEngine::error_count() const noexcept {
    return error_count_;
}

std::span<const Diagnostic> DiagnosticEngine::diagnostics() const noexcept {
    return diagnostics_;
}

void render_diagnostics(
    std::ostream& output,
    std::span<const Diagnostic> diagnostics,
    const SourceManager& sources) {
    for (const auto& diagnostic : diagnostics) {
        if (diagnostic.primary_span.has_value()) {
            render_source_line(output, diagnostic, sources);
            continue;
        }

        output << "pseudo: " << severity_name(diagnostic.severity) << ": "
               << diagnostic.message << '\n';
    }
}

}

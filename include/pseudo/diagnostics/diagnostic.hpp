#pragma once

#include "pseudo/common/source_span.hpp"

#include <optional>
#include <string>

namespace tpp {

enum class DiagnosticSeverity {
    note,
    warning,
    error,
};

struct Diagnostic {
    DiagnosticSeverity severity;
    std::string message;
    std::optional<SourceSpan> primary_span;
};

}

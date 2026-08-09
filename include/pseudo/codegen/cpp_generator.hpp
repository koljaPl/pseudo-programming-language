#pragma once

#include <optional>
#include <string>

namespace tpp {

class DiagnosticEngine;
struct Program;

[[nodiscard]] std::optional<std::string> generate_cpp(
    const Program& program,
    DiagnosticEngine& diagnostics);

}

#pragma once

#include <optional>
#include <string>

namespace tpp {

class DiagnosticEngine;
class TypeContext;
struct LoweredProgram;

// `types` must be the TypeContext that supplied the TypeIds stored in
// `program` during lowering.
[[nodiscard]] std::optional<std::string> generate_cpp(
    const LoweredProgram& program,
    const TypeContext& types,
    DiagnosticEngine& diagnostics);

}

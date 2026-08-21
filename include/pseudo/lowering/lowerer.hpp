#pragma once

#include "pseudo/lowering/lowered_ir.hpp"

#include <optional>

namespace tpp {

class DeclarationInfo;
class DiagnosticEngine;
class ResolutionInfo;
class SymbolTable;
class TypeContext;
class TypeInfo;
struct Program;

struct LoweringContext {
    const TypeContext& types;
    const SymbolTable& symbols;
    const DeclarationInfo& declarations;
    const ResolutionInfo& resolutions;
    const TypeInfo& type_info;
};

[[nodiscard]] std::optional<LoweredProgram> lower_program(
    const Program& program,
    const LoweringContext& context,
    DiagnosticEngine& diagnostics);

}

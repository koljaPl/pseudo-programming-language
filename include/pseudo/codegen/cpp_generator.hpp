#pragma once

#include <optional>
#include <string>

namespace tpp {

class DeclarationInfo;
class DiagnosticEngine;
class ResolutionInfo;
class SymbolTable;
class TypeContext;
class TypeInfo;
struct Program;

struct CppGenerationContext {
    const TypeContext& types;
    const SymbolTable& symbols;
    const DeclarationInfo& declarations;
    const ResolutionInfo& resolutions;
    const TypeInfo& type_info;
};

[[nodiscard]] std::optional<std::string> generate_cpp(
    const Program& program,
    const CppGenerationContext& context,
    DiagnosticEngine& diagnostics);

}

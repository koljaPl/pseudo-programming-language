#pragma once

#include "pseudo/ast/program.hpp"

namespace tpp {

class DeclarationInfo;
class DiagnosticEngine;
class SymbolTable;
class TypeContext;

// Validation is a one-shot pass over a stable Program after successful
// declaration collection. It does not perform name resolution.
class EntryPointValidator {
public:
    EntryPointValidator(
        const TypeContext& types,
        const SymbolTable& symbols,
        const DeclarationInfo& declarations,
        DiagnosticEngine& diagnostics);

    [[nodiscard]] bool validate(const Program& program);

private:
    const TypeContext& types_;
    const SymbolTable& symbols_;
    const DeclarationInfo& declarations_;
    DiagnosticEngine& diagnostics_;
};

}

#include "pseudo/semantic/entry_point_validator.hpp"

#include "pseudo/diagnostics/diagnostic_engine.hpp"
#include "pseudo/semantic/declaration_info.hpp"
#include "pseudo/semantic/symbol_table.hpp"
#include "pseudo/semantic/type_context.hpp"

#include <string_view>
#include <variant>

namespace tpp {

EntryPointValidator::EntryPointValidator(
    const TypeContext& types,
    const SymbolTable& symbols,
    const DeclarationInfo& declarations,
    DiagnosticEngine& diagnostics)
    : types_{types},
      symbols_{symbols},
      declarations_{declarations},
      diagnostics_{diagnostics} {}

bool EntryPointValidator::validate(const Program& program) {
    constexpr std::string_view entry_point_name{"main"};

    const FunctionDeclaration* entry_point = nullptr;
    for (const auto& declaration : program.declarations) {
        const auto* function = std::get_if<FunctionDeclaration>(&declaration);
        if (function == nullptr || function->name != entry_point_name) {
            continue;
        }

        entry_point = function;
        break;
    }

    if (entry_point == nullptr) {
        diagnostics_.error(
            program.span,
            "program requires exactly one top-level 'int main()' function");
        return false;
    }

    const auto symbol = declarations_.symbol_for(*entry_point);
    if (!symbol.has_value()) {
        diagnostics_.error(
            program.span,
            "program requires exactly one top-level 'int main()' function");
        return false;
    }

    const auto* function = std::get_if<FunctionSymbol>(
        &symbols_.symbol(*symbol).data);
    if (function == nullptr
        || function->return_type != types_.integer_type()
        || !function->parameter_types.empty()) {
        diagnostics_.error(
            entry_point->name_span,
            "'main' must have return type 'int' and no parameters");
        return false;
    }

    return true;
}

}

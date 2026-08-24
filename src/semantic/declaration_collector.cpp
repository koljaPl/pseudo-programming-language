#include "pseudo/semantic/declaration_collector.hpp"

#include "pseudo/diagnostics/diagnostic_engine.hpp"
#include "pseudo/semantic/ast_type.hpp"
#include "pseudo/semantic/type_context.hpp"

#include <cstddef>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace tpp {

DeclarationCollector::DeclarationCollector(
    TypeContext& types,
    SymbolTable& symbols,
    DeclarationInfo& declarations,
    DiagnosticEngine& diagnostics)
    : types_{types},
      symbols_{symbols},
      declarations_{declarations},
      diagnostics_{diagnostics} {}

bool DeclarationCollector::collect(const Program& program) {
    const auto initial_error_count = diagnostics_.error_count();
    const auto global_scope = symbols_.global_scope();

    for (const auto& declaration : program.declarations) {
        collect_top_level(declaration, global_scope);
    }

    return diagnostics_.error_count() == initial_error_count;
}

void DeclarationCollector::collect_top_level(
    const TopLevelDeclaration& declaration,
    const ScopeId scope) {
    std::visit(
        [this, scope](const auto& node) {
            using Node = std::decay_t<decltype(node)>;

            if constexpr (std::is_same_v<Node, FunctionDeclaration>) {
                collect_function(node, scope);
            } else {
                collect_variable(node, scope);
            }
        },
        declaration);
}

void DeclarationCollector::collect_function(
    const FunctionDeclaration& declaration,
    const ScopeId enclosing_scope) {
    const auto return_type = type_id_for(types_, declaration.return_type);
    if (!return_type.has_value()) {
        report_invalid_type(declaration.return_type.span);
    }

    std::vector<std::optional<TypeId>> parameter_types;
    parameter_types.reserve(declaration.parameters.size());

    std::vector<TypeId> signature_parameter_types;
    signature_parameter_types.reserve(declaration.parameters.size());

    auto signature_is_valid = return_type.has_value();
    for (const auto& parameter : declaration.parameters) {
        auto parameter_type = type_id_for(types_, parameter.type);
        if (!parameter_type.has_value()) {
            report_invalid_type(parameter.type.span);
            signature_is_valid = false;
        } else {
            signature_parameter_types.push_back(*parameter_type);
        }

        parameter_types.push_back(parameter_type);
    }

    if (signature_is_valid) {
        auto symbol = insert_symbol(
            enclosing_scope,
            Symbol{
                .name = declaration.name,
                .declaration_span = declaration.name_span,
                .data = FunctionSymbol{
                    .return_type = *return_type,
                    .parameter_types =
                        std::move(signature_parameter_types),
                },
            });
        if (symbol.has_value()) {
            declarations_.record(declaration, *symbol);
        }
    }

    const auto function_scope =
        symbols_.create_child_scope(enclosing_scope);

    for (std::size_t index = 0;
         index < declaration.parameters.size();
         ++index) {
        if (!parameter_types[index].has_value()) {
            continue;
        }

        const auto& parameter = declaration.parameters[index];
        auto symbol = insert_symbol(
            function_scope,
            Symbol{
                .name = parameter.name,
                .declaration_span = parameter.name_span,
                .data = ParameterSymbol{*parameter_types[index]},
            });
        if (symbol.has_value()) {
            declarations_.record(parameter, *symbol);
        }
    }

    if (declaration.body == nullptr) {
        diagnostics_.error(
            declaration.span,
            "function declaration is missing a body");
        return;
    }

    collect_block(*declaration.body, function_scope);
}

void DeclarationCollector::collect_variable(
    const VariableDeclaration& declaration,
    const ScopeId scope) {
    const auto type = type_id_for(types_, declaration.type);
    if (!type.has_value()) {
        report_invalid_type(declaration.type.span);
        return;
    }

    auto symbol = insert_symbol(
        scope,
        Symbol{
            .name = declaration.name,
            .declaration_span = declaration.name_span,
            .data = VariableSymbol{*type},
        });
    if (symbol.has_value()) {
        declarations_.record(declaration, *symbol);
    }
}

void DeclarationCollector::collect_block(
    const Block& block,
    const ScopeId scope) {
    declarations_.record(block, scope);

    for (const auto& item : block.items) {
        collect_block_item(item, scope);
    }
}

void DeclarationCollector::collect_block_item(
    const BlockItem& item,
    const ScopeId scope) {
    std::visit(
        [this, scope](const auto& node) {
            using Node = std::decay_t<decltype(node)>;

            if constexpr (std::is_same_v<Node, FunctionDeclaration>) {
                collect_function(node, scope);
            } else {
                collect_statement(node, scope);
            }
        },
        item);
}

void DeclarationCollector::collect_statement(
    const Statement& statement,
    const ScopeId scope) {
    std::visit(
        [this, span = statement.span, scope](const auto& node) {
            using Node = std::decay_t<decltype(node)>;

            if constexpr (std::is_same_v<Node, VariableDeclaration>) {
                collect_variable(node, scope);
            } else if constexpr (std::is_same_v<Node, IfStatement>) {
                collect_child_block(
                    node.then_block,
                    scope,
                    span,
                    "if statement is missing a body");
                if (node.else_block != nullptr) {
                    collect_child_block(
                        node.else_block,
                        scope,
                        span,
                        "else branch is missing a body");
                }
            } else if constexpr (std::is_same_v<Node, WhileStatement>) {
                collect_child_block(
                    node.body,
                    scope,
                    span,
                    "while statement is missing a body");
            } else if constexpr (std::is_same_v<Node, ForRangeStatement>) {
                collect_for_range(node, span, scope);
            } else if constexpr (std::is_same_v<Node, ForEachStatement>) {
                collect_for_each(node, span, scope);
            } else if constexpr (std::is_same_v<Node, BlockStatement>) {
                collect_child_block(
                    node.block,
                    scope,
                    span,
                    "block statement is missing a block");
            }
        },
        statement.node);
}

void DeclarationCollector::collect_child_block(
    const BlockPtr& block,
    const ScopeId parent,
    const SourceSpan owner_span,
    const char* const missing_body_message) {
    if (block == nullptr) {
        diagnostics_.error(owner_span, missing_body_message);
        return;
    }

    const auto scope = symbols_.create_child_scope(parent);
    collect_block(*block, scope);
}

void DeclarationCollector::collect_for_range(
    const ForRangeStatement& statement,
    const SourceSpan span,
    const ScopeId enclosing_scope) {
    if (statement.body == nullptr) {
        diagnostics_.error(span, "for-range statement is missing a body");
        return;
    }

    const auto body_scope = symbols_.create_child_scope(enclosing_scope);
    auto symbol = insert_symbol(
        body_scope,
        Symbol{
            .name = statement.variable,
            .declaration_span = statement.variable_span,
            .data = VariableSymbol{types_.integer_type()},
        });
    if (symbol.has_value()) {
        declarations_.record(statement, *symbol);
    }

    collect_block(*statement.body, body_scope);
}

void DeclarationCollector::collect_for_each(
    const ForEachStatement& statement,
    const SourceSpan span,
    const ScopeId enclosing_scope) {
    if (statement.body == nullptr) {
        diagnostics_.error(span, "for-each statement is missing a body");
        return;
    }

    const auto body_scope = symbols_.create_child_scope(enclosing_scope);
    auto symbol = insert_symbol(
        body_scope,
        Symbol{
            .name = statement.variable,
            .declaration_span = statement.variable_span,
            .data = VariableSymbol{std::nullopt},
        });
    if (symbol.has_value()) {
        declarations_.record(statement, *symbol);
    }

    collect_block(*statement.body, body_scope);
}

std::optional<SymbolId> DeclarationCollector::insert_symbol(
    const ScopeId scope,
    Symbol symbol) {
    const auto name = symbol.name;
    const auto declaration_span = symbol.declaration_span;
    const auto result = symbols_.insert(scope, std::move(symbol));

    if (const auto* inserted = std::get_if<SymbolId>(&result)) {
        return *inserted;
    }

    const auto duplicate = std::get<DuplicateSymbol>(result);
    const auto previous_span =
        symbols_.symbol(duplicate.existing_symbol).declaration_span;
    diagnostics_.error(
        declaration_span,
        "duplicate declaration of '" + name + "'");
    diagnostics_.note(previous_span, "previous declaration is here");
    return std::nullopt;
}

void DeclarationCollector::report_invalid_type(const SourceSpan span) {
    diagnostics_.error(span, "invalid declared type");
}

}

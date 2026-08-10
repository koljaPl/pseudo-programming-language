#include "test_support.hpp"

#include "pseudo/semantic/symbol_table.hpp"

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

using tpp::DuplicateSymbol;
using tpp::FunctionSymbol;
using tpp::ParameterSymbol;
using tpp::ScopeId;
using tpp::SourceId;
using tpp::SourceSpan;
using tpp::Symbol;
using tpp::SymbolId;
using tpp::SymbolInsertResult;
using tpp::SymbolTable;
using tpp::TypeId;
using tpp::VariableSymbol;

constexpr SourceSpan declaration_span(
    std::size_t source,
    std::size_t begin,
    std::size_t end)
{
    return SourceSpan{SourceId{source}, begin, end};
}

Symbol variable_symbol(
    std::string name,
    TypeId type,
    SourceSpan span = declaration_span(0, 0, 1))
{
    return Symbol{
        .name = std::move(name),
        .declaration_span = span,
        .data = VariableSymbol{.type = type},
    };
}

SymbolId require_inserted(const SymbolInsertResult& result)
{
    const auto* id = std::get_if<SymbolId>(&result);
    TPP_CHECK(id != nullptr);
    return *id;
}

DuplicateSymbol require_duplicate(const SymbolInsertResult& result)
{
    const auto* duplicate = std::get_if<DuplicateSymbol>(&result);
    TPP_CHECK(duplicate != nullptr);
    return *duplicate;
}

template <typename Callable>
void require_out_of_range(Callable&& callable)
{
    try {
        std::forward<Callable>(callable)();
    } catch (const std::out_of_range&) {
        return;
    }

    throw tpp::test::Failure{"expected std::out_of_range"};
}

void table_starts_with_one_global_scope()
{
    const SymbolTable symbols;
    const auto global = symbols.global_scope();

    TPP_CHECK_EQ(global.value, std::size_t{0});
    TPP_CHECK_EQ(symbols.scope_count(), std::size_t{1});
    TPP_CHECK_EQ(symbols.symbol_count(), std::size_t{0});
    TPP_CHECK(!symbols.scope(global).parent().has_value());
}

void symbols_own_names_kinds_and_spans()
{
    SymbolTable symbols;
    std::string source_name{"value"};
    const auto span = declaration_span(3, 7, 12);
    const auto id = require_inserted(symbols.insert(
        symbols.global_scope(),
        Symbol{
            .name = source_name,
            .declaration_span = span,
            .data = ParameterSymbol{TypeId{2}},
        }));
    source_name.assign("changed");

    const auto& stored = symbols.symbol(id);
    TPP_CHECK_EQ(stored.name, std::string{"value"});
    TPP_CHECK_EQ(stored.declaration_span.source.value, std::size_t{3});
    TPP_CHECK_EQ(stored.declaration_span.begin, std::size_t{7});
    TPP_CHECK_EQ(stored.declaration_span.end, std::size_t{12});

    const auto* parameter = std::get_if<ParameterSymbol>(&stored.data);
    TPP_CHECK(parameter != nullptr);
    TPP_CHECK_EQ(parameter->type, TypeId{2});

    const auto variable_id = require_inserted(symbols.insert(
        symbols.global_scope(),
        variable_symbol(
            "other",
            TypeId{4},
            declaration_span(8, 13, 18))));
    const auto& variable = symbols.symbol(variable_id);
    const auto* variable_data = std::get_if<VariableSymbol>(&variable.data);
    TPP_CHECK(variable_data != nullptr);
    TPP_CHECK(variable_data->type.has_value());
    TPP_CHECK_EQ(*variable_data->type, TypeId{4});
    TPP_CHECK_EQ(variable.declaration_span.source.value, std::size_t{8});
    TPP_CHECK_EQ(variable.declaration_span.begin, std::size_t{13});
    TPP_CHECK_EQ(variable.declaration_span.end, std::size_t{18});
}

void variable_symbols_can_defer_their_type()
{
    SymbolTable symbols;
    const auto id = require_inserted(symbols.insert(
        symbols.global_scope(),
        Symbol{
            .name = "item",
            .declaration_span = declaration_span(2, 4, 8),
            .data = VariableSymbol{.type = std::nullopt},
        }));

    const auto* variable =
        std::get_if<VariableSymbol>(&symbols.symbol(id).data);
    TPP_CHECK(variable != nullptr);
    TPP_CHECK(!variable->type.has_value());
}

void function_signatures_preserve_parameter_order()
{
    SymbolTable symbols;
    const auto id = require_inserted(symbols.insert(
        symbols.global_scope(),
        Symbol{
            .name = "combine",
            .declaration_span = declaration_span(1, 2, 9),
            .data = FunctionSymbol{
                .return_type = TypeId{0},
                .parameter_types = {TypeId{1}, TypeId{3}, TypeId{2}},
            },
        }));

    const auto* function =
        std::get_if<FunctionSymbol>(&symbols.symbol(id).data);
    TPP_CHECK(function != nullptr);
    TPP_CHECK_EQ(function->return_type, TypeId{0});
    TPP_CHECK_EQ(function->parameter_types.size(), std::size_t{3});
    TPP_CHECK_EQ(function->parameter_types[0], TypeId{1});
    TPP_CHECK_EQ(function->parameter_types[1], TypeId{3});
    TPP_CHECK_EQ(function->parameter_types[2], TypeId{2});
    TPP_CHECK_EQ(
        symbols.symbol(id).declaration_span.source.value,
        std::size_t{1});
    TPP_CHECK_EQ(symbols.symbol(id).declaration_span.begin, std::size_t{2});
    TPP_CHECK_EQ(symbols.symbol(id).declaration_span.end, std::size_t{9});

    const auto empty_id = require_inserted(symbols.insert(
        symbols.global_scope(),
        Symbol{
            .name = "notify",
            .declaration_span = declaration_span(4, 20, 26),
            .data = FunctionSymbol{
                .return_type = TypeId{4},
                .parameter_types = {},
            },
        }));
    const auto* empty =
        std::get_if<FunctionSymbol>(&symbols.symbol(empty_id).data);
    TPP_CHECK(empty != nullptr);
    TPP_CHECK_EQ(empty->return_type, TypeId{4});
    TPP_CHECK(empty->parameter_types.empty());
}

void symbol_ids_remain_stable_as_the_table_grows()
{
    SymbolTable symbols;
    const auto global = symbols.global_scope();
    const auto first = require_inserted(symbols.insert(
        global,
        variable_symbol("first", TypeId{0}, declaration_span(0, 1, 6))));

    for (std::size_t index = 0; index < 256; ++index) {
        const auto result = symbols.insert(
            global,
            variable_symbol("symbol_" + std::to_string(index), TypeId{index}));
        TPP_CHECK(std::holds_alternative<SymbolId>(result));
    }

    TPP_CHECK_EQ(first.value, std::size_t{0});
    TPP_CHECK_EQ(symbols.symbol(first).name, std::string{"first"});
    TPP_CHECK_EQ(symbols.lookup(global, "first"), std::optional<SymbolId>{first});
}

void scopes_form_a_stable_parent_tree()
{
    SymbolTable symbols;
    const auto global = symbols.global_scope();
    const auto child = symbols.create_child_scope(global);
    const auto nested = symbols.create_child_scope(child);

    TPP_CHECK_EQ(child.value, std::size_t{1});
    TPP_CHECK_EQ(nested.value, std::size_t{2});
    TPP_CHECK_EQ(symbols.scope_count(), std::size_t{3});
    TPP_CHECK_EQ(symbols.scope(child).parent(), std::optional<ScopeId>{global});
    TPP_CHECK_EQ(symbols.scope(nested).parent(), std::optional<ScopeId>{child});

    for (std::size_t index = 0; index < 256; ++index) {
        static_cast<void>(symbols.create_child_scope(global));
    }

    TPP_CHECK_EQ(symbols.scope(child).parent(), std::optional<ScopeId>{global});
    TPP_CHECK_EQ(symbols.scope(nested).parent(), std::optional<ScopeId>{child});
}

void recursive_lookup_walks_parent_scopes()
{
    SymbolTable symbols;
    const auto global = symbols.global_scope();
    const auto child = symbols.create_child_scope(global);
    const auto nested = symbols.create_child_scope(child);
    const auto global_value = require_inserted(symbols.insert(
        global,
        variable_symbol("global_value", TypeId{0})));
    const auto child_value = require_inserted(symbols.insert(
        child,
        variable_symbol("child_value", TypeId{1})));

    TPP_CHECK(!symbols.lookup_local(nested, "global_value").has_value());
    TPP_CHECK(!symbols.lookup_local(nested, "child_value").has_value());
    TPP_CHECK_EQ(
        symbols.lookup(nested, "global_value"),
        std::optional<SymbolId>{global_value});
    TPP_CHECK_EQ(
        symbols.lookup(nested, "child_value"),
        std::optional<SymbolId>{child_value});
    TPP_CHECK(!symbols.lookup(nested, "missing").has_value());
}

void child_declarations_shadow_parent_declarations()
{
    SymbolTable symbols;
    const auto global = symbols.global_scope();
    const auto child = symbols.create_child_scope(global);
    const auto outer = require_inserted(symbols.insert(
        global,
        variable_symbol("value", TypeId{0})));
    const auto inner = require_inserted(symbols.insert(
        child,
        variable_symbol("value", TypeId{1})));

    TPP_CHECK(outer != inner);
    TPP_CHECK_EQ(symbols.lookup(global, "value"), std::optional<SymbolId>{outer});
    TPP_CHECK_EQ(symbols.lookup(child, "value"), std::optional<SymbolId>{inner});
    TPP_CHECK_EQ(
        symbols.lookup_local(child, "value"),
        std::optional<SymbolId>{inner});
}

void duplicate_names_share_one_namespace_and_do_not_consume_ids()
{
    SymbolTable symbols;
    const auto global = symbols.global_scope();
    const auto original = require_inserted(symbols.insert(
        global,
        variable_symbol("item", TypeId{0}, declaration_span(0, 1, 5))));
    const auto duplicate = require_duplicate(symbols.insert(
        global,
        Symbol{
            .name = "item",
            .declaration_span = declaration_span(0, 10, 14),
            .data = FunctionSymbol{
                .return_type = TypeId{4},
                .parameter_types = {},
            },
        }));

    TPP_CHECK_EQ(duplicate.existing_symbol, original);
    TPP_CHECK_EQ(symbols.symbol_count(), std::size_t{1});
    const auto* variable =
        std::get_if<VariableSymbol>(&symbols.symbol(original).data);
    TPP_CHECK(variable != nullptr);
    TPP_CHECK(variable->type.has_value());
    TPP_CHECK_EQ(*variable->type, TypeId{0});
    TPP_CHECK_EQ(symbols.symbol(original).declaration_span.begin, std::size_t{1});

    const auto next = require_inserted(symbols.insert(
        global,
        variable_symbol("next", TypeId{0})));
    TPP_CHECK_EQ(next.value, std::size_t{1});
}

void sibling_scopes_are_independent()
{
    SymbolTable symbols;
    const auto global = symbols.global_scope();
    const auto left = symbols.create_child_scope(global);
    const auto right = symbols.create_child_scope(global);
    const auto left_value = require_inserted(symbols.insert(
        left,
        variable_symbol("value", TypeId{1})));
    const auto shared = require_inserted(symbols.insert(
        global,
        variable_symbol("shared", TypeId{0})));

    TPP_CHECK_EQ(
        symbols.lookup(left, "value"),
        std::optional<SymbolId>{left_value});
    TPP_CHECK(!symbols.lookup(right, "value").has_value());
    TPP_CHECK_EQ(
        symbols.lookup(left, "shared"),
        std::optional<SymbolId>{shared});
    TPP_CHECK_EQ(
        symbols.lookup(right, "shared"),
        std::optional<SymbolId>{shared});

    const auto right_value = require_inserted(symbols.insert(
        right,
        variable_symbol("value", TypeId{2})));
    TPP_CHECK(left_value != right_value);
    TPP_CHECK_EQ(
        symbols.lookup(right, "value"),
        std::optional<SymbolId>{right_value});
}

void invalid_ids_are_rejected_without_mutation()
{
    SymbolTable symbols;
    const ScopeId invalid_scope{99};
    const SymbolId invalid_symbol{99};

    require_out_of_range([&] {
        static_cast<void>(symbols.scope(invalid_scope));
    });
    require_out_of_range([&] {
        static_cast<void>(symbols.symbol(invalid_symbol));
    });
    require_out_of_range([&] {
        static_cast<void>(symbols.create_child_scope(invalid_scope));
    });
    require_out_of_range([&] {
        static_cast<void>(symbols.insert(
            invalid_scope,
            variable_symbol("value", TypeId{0})));
    });
    require_out_of_range([&] {
        static_cast<void>(symbols.lookup_local(invalid_scope, "value"));
    });
    require_out_of_range([&] {
        static_cast<void>(symbols.lookup(invalid_scope, "value"));
    });

    TPP_CHECK_EQ(symbols.scope_count(), std::size_t{1});
    TPP_CHECK_EQ(symbols.symbol_count(), std::size_t{0});
}

}

int main()
{
    return tpp::test::run({
        {"table starts with one global scope", table_starts_with_one_global_scope},
        {"symbols own names, kinds, and spans", symbols_own_names_kinds_and_spans},
        {"variable symbols can defer their type",
         variable_symbols_can_defer_their_type},
        {"function signatures preserve parameter order",
         function_signatures_preserve_parameter_order},
        {"symbol IDs remain stable as the table grows",
         symbol_ids_remain_stable_as_the_table_grows},
        {"scopes form a stable parent tree", scopes_form_a_stable_parent_tree},
        {"recursive lookup walks parent scopes", recursive_lookup_walks_parent_scopes},
        {"child declarations shadow parent declarations",
         child_declarations_shadow_parent_declarations},
        {"duplicate names share one namespace and do not consume IDs",
         duplicate_names_share_one_namespace_and_do_not_consume_ids},
        {"sibling scopes are independent", sibling_scopes_are_independent},
        {"invalid IDs are rejected without mutation", invalid_ids_are_rejected_without_mutation},
    });
}

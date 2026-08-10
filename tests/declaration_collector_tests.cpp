#include "test_support.hpp"

#include "pseudo/ast/program.hpp"
#include "pseudo/diagnostics/diagnostic.hpp"
#include "pseudo/diagnostics/diagnostic_engine.hpp"
#include "pseudo/lexer/lexer.hpp"
#include "pseudo/parser/parser.hpp"
#include "pseudo/semantic/declaration_collector.hpp"
#include "pseudo/semantic/declaration_info.hpp"
#include "pseudo/semantic/symbol_table.hpp"
#include "pseudo/semantic/type_context.hpp"
#include "pseudo/source/source_manager.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

using tpp::Block;
using tpp::BlockItem;
using tpp::BlockStatement;
using tpp::DeclarationCollector;
using tpp::DeclarationInfo;
using tpp::DiagnosticEngine;
using tpp::DiagnosticSeverity;
using tpp::ForEachStatement;
using tpp::ForRangeStatement;
using tpp::FunctionDeclaration;
using tpp::FunctionSymbol;
using tpp::IfStatement;
using tpp::Lexer;
using tpp::Parameter;
using tpp::ParameterSymbol;
using tpp::Parser;
using tpp::Program;
using tpp::ScopeId;
using tpp::SourceId;
using tpp::SourceManager;
using tpp::SourceSpan;
using tpp::Statement;
using tpp::Symbol;
using tpp::SymbolId;
using tpp::SymbolTable;
using tpp::Token;
using tpp::TypeContext;
using tpp::TypeId;
using tpp::ValueType;
using tpp::VariableDeclaration;
using tpp::VariableSymbol;
using tpp::VectorType;
using tpp::WhileStatement;

class CollectionResult {
public:
    explicit CollectionResult(std::string contents)
        : program{SourceSpan{SourceId{0}, 0, 0}, {}}
    {
        source = sources.add_source("program.tpp", std::move(contents));

        Lexer lexer{source, sources, diagnostics};
        tokens = lexer.lex();
        TPP_CHECK(!diagnostics.has_errors());

        Parser parser{tokens, sources, diagnostics};
        program = parser.parse_program();
        TPP_CHECK(!diagnostics.has_errors());

        DeclarationCollector collector{
            types,
            symbols,
            declarations,
            diagnostics,
        };
        succeeded = collector.collect(program);
    }

    SourceManager sources;
    DiagnosticEngine diagnostics;
    SourceId source{0};
    std::vector<Token> tokens;
    Program program;
    TypeContext types;
    SymbolTable symbols;
    DeclarationInfo declarations;
    bool succeeded = false;
};

template <typename Node, typename Variant>
const Node& require_variant(const Variant& variant)
{
    const auto* node = std::get_if<Node>(&variant);
    TPP_CHECK(node != nullptr);
    return *node;
}

const FunctionDeclaration& require_function(
    const tpp::TopLevelDeclaration& declaration)
{
    return require_variant<FunctionDeclaration>(declaration);
}

const VariableDeclaration& require_variable(
    const tpp::TopLevelDeclaration& declaration)
{
    return require_variant<VariableDeclaration>(declaration);
}

const Statement& require_statement(const BlockItem& item)
{
    return require_variant<Statement>(item);
}

const FunctionDeclaration& require_nested_function(const BlockItem& item)
{
    return require_variant<FunctionDeclaration>(item);
}

template <typename Node>
const Node& require_statement_node(const BlockItem& item)
{
    return require_variant<Node>(require_statement(item).node);
}

SymbolId require_symbol_id(const std::optional<SymbolId> id)
{
    TPP_CHECK(id.has_value());
    return *id;
}

ScopeId require_scope_id(const std::optional<ScopeId> id)
{
    TPP_CHECK(id.has_value());
    return *id;
}

TypeId require_type_id(const std::optional<TypeId> id)
{
    TPP_CHECK(id.has_value());
    return *id;
}

template <typename Data>
const Data& require_symbol_data(const SymbolTable& symbols, const SymbolId id)
{
    return require_variant<Data>(symbols.symbol(id).data);
}

TypeId require_known_variable_type(
    const SymbolTable& symbols,
    const SymbolId id)
{
    const auto& variable = require_symbol_data<VariableSymbol>(symbols, id);
    TPP_CHECK(variable.type.has_value());
    return *variable.type;
}

void check_span(const SourceSpan actual, const SourceSpan expected)
{
    TPP_CHECK_EQ(actual.source, expected.source);
    TPP_CHECK_EQ(actual.begin, expected.begin);
    TPP_CHECK_EQ(actual.end, expected.end);
}

void globals_functions_parameters_and_locals_are_collected()
{
    CollectionResult result{
        "int count;"
        "vector<string> names;"
        "vector<vector<int>> transform("
        "vector<vector<char>> input, bool enabled) {"
        "string text;"
        "vector<int> values;"
        "}"
        "void notify() {}"};

    TPP_CHECK(result.succeeded);
    TPP_CHECK(!result.diagnostics.has_errors());
    TPP_CHECK(!result.declarations.empty());
    TPP_CHECK_EQ(result.program.declarations.size(), std::size_t{4});
    TPP_CHECK_EQ(result.symbols.scope_count(), std::size_t{3});
    TPP_CHECK_EQ(result.symbols.symbol_count(), std::size_t{8});

    const auto& count = require_variable(result.program.declarations[0]);
    const auto& names = require_variable(result.program.declarations[1]);
    const auto& function = require_function(result.program.declarations[2]);
    const auto& notify = require_function(result.program.declarations[3]);
    const auto global = result.symbols.global_scope();

    const auto count_id = require_symbol_id(result.declarations.symbol_for(count));
    const auto names_id = require_symbol_id(result.declarations.symbol_for(names));
    const auto function_id =
        require_symbol_id(result.declarations.symbol_for(function));
    const auto notify_id =
        require_symbol_id(result.declarations.symbol_for(notify));
    TPP_CHECK_EQ(result.symbols.lookup_local(global, "count"), count_id);
    TPP_CHECK_EQ(result.symbols.lookup_local(global, "names"), names_id);
    TPP_CHECK_EQ(result.symbols.lookup_local(global, "transform"), function_id);
    TPP_CHECK_EQ(result.symbols.lookup_local(global, "notify"), notify_id);
    TPP_CHECK_EQ(
        require_known_variable_type(result.symbols, count_id),
        result.types.integer_type());
    check_span(result.symbols.symbol(count_id).declaration_span, count.name_span);
    check_span(result.symbols.symbol(names_id).declaration_span, names.name_span);
    check_span(
        result.symbols.symbol(function_id).declaration_span,
        function.name_span);

    const auto vector_string =
        require_type_id(result.types.vector_type(result.types.string_type()));
    TPP_CHECK_EQ(
        require_known_variable_type(result.symbols, names_id),
        vector_string);

    const auto vector_integer =
        require_type_id(result.types.vector_type(result.types.integer_type()));
    const auto nested_integer =
        require_type_id(result.types.vector_type(vector_integer));
    const auto vector_character =
        require_type_id(result.types.vector_type(result.types.character_type()));
    const auto nested_character =
        require_type_id(result.types.vector_type(vector_character));
    const auto& signature =
        require_symbol_data<FunctionSymbol>(result.symbols, function_id);
    TPP_CHECK_EQ(signature.return_type, nested_integer);
    TPP_CHECK_EQ(signature.parameter_types.size(), std::size_t{2});
    TPP_CHECK_EQ(signature.parameter_types[0], nested_character);
    TPP_CHECK_EQ(signature.parameter_types[1], result.types.boolean_type());
    const auto& notify_signature =
        require_symbol_data<FunctionSymbol>(result.symbols, notify_id);
    TPP_CHECK_EQ(notify_signature.return_type, result.types.void_type());
    TPP_CHECK(notify_signature.parameter_types.empty());
    TPP_CHECK(notify.body != nullptr);
    const auto notify_scope =
        require_scope_id(result.declarations.scope_for(*notify.body));
    TPP_CHECK_EQ(
        result.symbols.scope(notify_scope).parent(),
        std::optional<ScopeId>{global});

    TPP_CHECK(function.body != nullptr);
    const auto function_scope =
        require_scope_id(result.declarations.scope_for(*function.body));
    TPP_CHECK_EQ(
        result.symbols.scope(function_scope).parent(),
        std::optional<ScopeId>{global});

    TPP_CHECK_EQ(function.parameters.size(), std::size_t{2});
    const Parameter& input = function.parameters[0];
    const Parameter& enabled = function.parameters[1];
    const auto input_id =
        require_symbol_id(result.declarations.symbol_for(input));
    const auto enabled_id =
        require_symbol_id(result.declarations.symbol_for(enabled));
    TPP_CHECK_EQ(result.symbols.lookup_local(function_scope, "input"), input_id);
    TPP_CHECK_EQ(result.symbols.lookup_local(function_scope, "enabled"), enabled_id);
    TPP_CHECK_EQ(
        require_symbol_data<ParameterSymbol>(result.symbols, input_id).type,
        nested_character);
    TPP_CHECK_EQ(
        require_symbol_data<ParameterSymbol>(result.symbols, enabled_id).type,
        result.types.boolean_type());
    check_span(
        result.symbols.symbol(input_id).declaration_span,
        input.name_span);
    check_span(
        result.symbols.symbol(enabled_id).declaration_span,
        enabled.name_span);

    TPP_CHECK_EQ(function.body->items.size(), std::size_t{2});
    const auto& text =
        require_statement_node<VariableDeclaration>(function.body->items[0]);
    const auto& values =
        require_statement_node<VariableDeclaration>(function.body->items[1]);
    const auto text_id = require_symbol_id(result.declarations.symbol_for(text));
    const auto values_id =
        require_symbol_id(result.declarations.symbol_for(values));
    TPP_CHECK_EQ(result.symbols.lookup_local(function_scope, "text"), text_id);
    TPP_CHECK_EQ(result.symbols.lookup_local(function_scope, "values"), values_id);
    TPP_CHECK_EQ(
        require_known_variable_type(result.symbols, text_id),
        result.types.string_type());
    TPP_CHECK_EQ(
        require_known_variable_type(result.symbols, values_id),
        vector_integer);
}

void nested_functions_blocks_shadowing_and_siblings_form_lexical_scopes()
{
    CollectionResult result{
        "void outer(int value) {"
        "int local;"
        "void nested(string text) {"
        "bool flag;"
        "{ char value; }"
        "}"
        "{ int sibling; }"
        "{ bool sibling; }"
        "}"};

    TPP_CHECK(result.succeeded);
    TPP_CHECK_EQ(result.symbols.scope_count(), std::size_t{6});
    const auto& outer = require_function(result.program.declarations.front());
    const auto outer_scope =
        require_scope_id(result.declarations.scope_for(*outer.body));
    const auto outer_parameter =
        require_symbol_id(result.declarations.symbol_for(outer.parameters.front()));
    TPP_CHECK_EQ(
        result.symbols.lookup_local(outer_scope, "value"),
        outer_parameter);

    const auto& nested = require_nested_function(outer.body->items[1]);
    const auto nested_id =
        require_symbol_id(result.declarations.symbol_for(nested));
    TPP_CHECK_EQ(result.symbols.lookup_local(outer_scope, "nested"), nested_id);
    const auto nested_scope =
        require_scope_id(result.declarations.scope_for(*nested.body));
    TPP_CHECK_EQ(
        result.symbols.scope(nested_scope).parent(),
        std::optional<ScopeId>{outer_scope});

    const auto nested_parameter =
        require_symbol_id(result.declarations.symbol_for(nested.parameters.front()));
    const auto& nested_local =
        require_statement_node<VariableDeclaration>(nested.body->items[0]);
    const auto nested_local_id =
        require_symbol_id(result.declarations.symbol_for(nested_local));
    TPP_CHECK_EQ(
        result.symbols.lookup_local(nested_scope, "text"),
        nested_parameter);
    TPP_CHECK_EQ(
        result.symbols.lookup_local(nested_scope, "flag"),
        nested_local_id);
    TPP_CHECK_EQ(
        result.symbols.lookup(nested_scope, "value"),
        outer_parameter);

    const auto& shadow_block =
        require_statement_node<BlockStatement>(nested.body->items[1]);
    const auto shadow_scope =
        require_scope_id(result.declarations.scope_for(*shadow_block.block));
    const auto& shadow = require_statement_node<VariableDeclaration>(
        shadow_block.block->items.front());
    const auto shadow_id =
        require_symbol_id(result.declarations.symbol_for(shadow));
    TPP_CHECK_EQ(
        result.symbols.scope(shadow_scope).parent(),
        std::optional<ScopeId>{nested_scope});
    TPP_CHECK_EQ(result.symbols.lookup(shadow_scope, "value"), shadow_id);
    TPP_CHECK(shadow_id != outer_parameter);

    const auto& left_block =
        require_statement_node<BlockStatement>(outer.body->items[2]);
    const auto& right_block =
        require_statement_node<BlockStatement>(outer.body->items[3]);
    const auto left_scope =
        require_scope_id(result.declarations.scope_for(*left_block.block));
    const auto right_scope =
        require_scope_id(result.declarations.scope_for(*right_block.block));
    TPP_CHECK(left_scope != right_scope);
    TPP_CHECK_EQ(
        result.symbols.scope(left_scope).parent(),
        std::optional<ScopeId>{outer_scope});
    TPP_CHECK_EQ(
        result.symbols.scope(right_scope).parent(),
        std::optional<ScopeId>{outer_scope});

    const auto left_sibling =
        require_symbol_id(result.symbols.lookup_local(left_scope, "sibling"));
    const auto right_sibling =
        require_symbol_id(result.symbols.lookup_local(right_scope, "sibling"));
    TPP_CHECK(left_sibling != right_sibling);
    TPP_CHECK_EQ(
        require_known_variable_type(result.symbols, left_sibling),
        result.types.integer_type());
    TPP_CHECK_EQ(
        require_known_variable_type(result.symbols, right_sibling),
        result.types.boolean_type());
    TPP_CHECK(!result.symbols.lookup_local(outer_scope, "sibling").has_value());
}

void control_flow_blocks_are_independent_child_scopes()
{
    CollectionResult result{
        "void flow() {"
        "if true { int branch; } else { bool branch; }"
        "while true { char current; }"
        "}"};

    TPP_CHECK(result.succeeded);
    const auto& function = require_function(result.program.declarations.front());
    const auto function_scope =
        require_scope_id(result.declarations.scope_for(*function.body));
    const auto& condition =
        require_statement_node<IfStatement>(function.body->items[0]);
    const auto& loop =
        require_statement_node<WhileStatement>(function.body->items[1]);
    TPP_CHECK(condition.then_block != nullptr);
    TPP_CHECK(condition.else_block != nullptr);
    TPP_CHECK(loop.body != nullptr);

    const std::array scopes{
        require_scope_id(result.declarations.scope_for(*condition.then_block)),
        require_scope_id(result.declarations.scope_for(*condition.else_block)),
        require_scope_id(result.declarations.scope_for(*loop.body)),
    };
    TPP_CHECK(scopes[0] != scopes[1]);
    TPP_CHECK(scopes[0] != scopes[2]);
    TPP_CHECK(scopes[1] != scopes[2]);
    for (const auto scope : scopes) {
        TPP_CHECK_EQ(
            result.symbols.scope(scope).parent(),
            std::optional<ScopeId>{function_scope});
    }

    const auto then_branch =
        require_symbol_id(result.symbols.lookup_local(scopes[0], "branch"));
    const auto else_branch =
        require_symbol_id(result.symbols.lookup_local(scopes[1], "branch"));
    const auto current =
        require_symbol_id(result.symbols.lookup_local(scopes[2], "current"));
    TPP_CHECK(then_branch != else_branch);
    TPP_CHECK_EQ(
        require_known_variable_type(result.symbols, then_branch),
        result.types.integer_type());
    TPP_CHECK_EQ(
        require_known_variable_type(result.symbols, else_branch),
        result.types.boolean_type());
    TPP_CHECK_EQ(
        require_known_variable_type(result.symbols, current),
        result.types.character_type());
    TPP_CHECK(!result.symbols.lookup_local(function_scope, "branch").has_value());
    TPP_CHECK(!result.symbols.lookup_local(function_scope, "current").has_value());
}

void duplicate_declarations_share_a_namespace_and_recover()
{
    CollectionResult result{
        "int shared;"
        "int shared;"
        "void shared() {}"
        "void f(int value, bool value) {"
        "int value;"
        "void value() {}"
        "}"
        "void f() { int recovered; }"};

    TPP_CHECK(!result.succeeded);
    TPP_CHECK(result.diagnostics.has_errors());
    TPP_CHECK_EQ(result.diagnostics.error_count(), std::size_t{6});
    TPP_CHECK_EQ(result.diagnostics.diagnostics().size(), std::size_t{12});
    TPP_CHECK_EQ(result.symbols.symbol_count(), std::size_t{4});

    const auto& first_shared = require_variable(result.program.declarations[0]);
    const auto& duplicate_shared = require_variable(result.program.declarations[1]);
    const auto& cross_kind = require_function(result.program.declarations[2]);
    const auto& first_function = require_function(result.program.declarations[3]);
    const auto& duplicate_function = require_function(result.program.declarations[4]);

    TPP_CHECK(result.declarations.symbol_for(first_shared).has_value());
    TPP_CHECK(!result.declarations.symbol_for(duplicate_shared).has_value());
    TPP_CHECK(!result.declarations.symbol_for(cross_kind).has_value());
    TPP_CHECK(result.declarations.symbol_for(first_function).has_value());
    TPP_CHECK(!result.declarations.symbol_for(duplicate_function).has_value());

    TPP_CHECK_EQ(first_function.parameters.size(), std::size_t{2});
    TPP_CHECK(result.declarations.symbol_for(first_function.parameters[0]).has_value());
    TPP_CHECK(!result.declarations.symbol_for(first_function.parameters[1]).has_value());
    const auto& duplicate_local = require_statement_node<VariableDeclaration>(
        first_function.body->items[0]);
    const auto& duplicate_nested =
        require_nested_function(first_function.body->items[1]);
    TPP_CHECK(!result.declarations.symbol_for(duplicate_local).has_value());
    TPP_CHECK(!result.declarations.symbol_for(duplicate_nested).has_value());

    TPP_CHECK(result.declarations.scope_for(*cross_kind.body).has_value());
    TPP_CHECK(result.declarations.scope_for(*first_function.body).has_value());
    TPP_CHECK(result.declarations.scope_for(*duplicate_nested.body).has_value());
    const auto duplicate_function_scope = require_scope_id(
        result.declarations.scope_for(*duplicate_function.body));
    const auto& recovered = require_statement_node<VariableDeclaration>(
        duplicate_function.body->items.front());
    const auto recovered_id =
        require_symbol_id(result.declarations.symbol_for(recovered));
    TPP_CHECK_EQ(
        result.symbols.lookup_local(duplicate_function_scope, "recovered"),
        recovered_id);

    constexpr std::array expected_names{
        std::string_view{"shared"},
        std::string_view{"shared"},
        std::string_view{"value"},
        std::string_view{"value"},
        std::string_view{"value"},
        std::string_view{"f"},
    };
    const auto diagnostics = result.diagnostics.diagnostics();
    for (std::size_t index = 0; index < expected_names.size(); ++index) {
        const auto& error = diagnostics[index * 2];
        const auto& note = diagnostics[index * 2 + 1];
        TPP_CHECK_EQ(error.severity, DiagnosticSeverity::error);
        TPP_CHECK_EQ(note.severity, DiagnosticSeverity::note);
        TPP_CHECK_EQ(
            error.message,
            std::string{"duplicate declaration of '"}
                + std::string{expected_names[index]} + "'");
        TPP_CHECK_EQ(note.message, std::string{"previous declaration is here"});
        TPP_CHECK(error.primary_span.has_value());
        TPP_CHECK(note.primary_span.has_value());
        TPP_CHECK_EQ(
            result.sources.slice(*error.primary_span),
            expected_names[index]);
        TPP_CHECK_EQ(
            result.sources.slice(*note.primary_span),
            expected_names[index]);
    }
}

void loop_bindings_have_body_scope_and_deferred_foreach_type()
{
    CollectionResult result{
        "void loops() {"
        "int outer;"
        "for i in 0..10 {"
        "int value;"
        "{ bool i; }"
        "}"
        "for item in values { string local; }"
        "}"};

    TPP_CHECK(result.succeeded);
    TPP_CHECK(!result.diagnostics.has_errors());
    const auto& function = require_function(result.program.declarations.front());
    const auto function_scope =
        require_scope_id(result.declarations.scope_for(*function.body));
    const auto& range =
        require_statement_node<ForRangeStatement>(function.body->items[1]);
    const auto& each =
        require_statement_node<ForEachStatement>(function.body->items[2]);

    const auto range_scope =
        require_scope_id(result.declarations.scope_for(*range.body));
    const auto range_binding =
        require_symbol_id(result.declarations.symbol_for(range));
    TPP_CHECK_EQ(
        result.symbols.scope(range_scope).parent(),
        std::optional<ScopeId>{function_scope});
    TPP_CHECK_EQ(
        result.symbols.lookup_local(range_scope, "i"),
        range_binding);
    TPP_CHECK_EQ(
        require_known_variable_type(result.symbols, range_binding),
        result.types.integer_type());
    check_span(
        result.symbols.symbol(range_binding).declaration_span,
        range.variable_span);

    const auto& value =
        require_statement_node<VariableDeclaration>(range.body->items[0]);
    const auto value_id =
        require_symbol_id(result.declarations.symbol_for(value));
    TPP_CHECK_EQ(result.symbols.lookup_local(range_scope, "value"), value_id);

    const auto& nested_block =
        require_statement_node<BlockStatement>(range.body->items[1]);
    const auto nested_scope =
        require_scope_id(result.declarations.scope_for(*nested_block.block));
    const auto nested_binding =
        require_symbol_id(result.symbols.lookup_local(nested_scope, "i"));
    TPP_CHECK(nested_binding != range_binding);
    TPP_CHECK_EQ(
        require_known_variable_type(result.symbols, nested_binding),
        result.types.boolean_type());

    const auto each_scope =
        require_scope_id(result.declarations.scope_for(*each.body));
    const auto each_binding =
        require_symbol_id(result.declarations.symbol_for(each));
    TPP_CHECK_EQ(
        result.symbols.scope(each_scope).parent(),
        std::optional<ScopeId>{function_scope});
    TPP_CHECK_EQ(
        result.symbols.lookup_local(each_scope, "item"),
        each_binding);
    const auto& each_variable =
        require_symbol_data<VariableSymbol>(result.symbols, each_binding);
    TPP_CHECK(!each_variable.type.has_value());
    check_span(
        result.symbols.symbol(each_binding).declaration_span,
        each.variable_span);

    const auto& local =
        require_statement_node<VariableDeclaration>(each.body->items.front());
    const auto local_id =
        require_symbol_id(result.declarations.symbol_for(local));
    TPP_CHECK_EQ(result.symbols.lookup_local(each_scope, "local"), local_id);
    TPP_CHECK(!result.symbols.lookup_local(function_scope, "i").has_value());
    TPP_CHECK(!result.symbols.lookup_local(function_scope, "item").has_value());
}

void loop_body_declarations_conflict_with_their_bindings()
{
    CollectionResult result{
        "void loops() {"
        "for i in 0..2 { int i; }"
        "for item in values { bool item; }"
        "}"};

    TPP_CHECK(!result.succeeded);
    TPP_CHECK_EQ(result.diagnostics.error_count(), std::size_t{2});
    TPP_CHECK_EQ(result.diagnostics.diagnostics().size(), std::size_t{4});
    TPP_CHECK_EQ(result.symbols.symbol_count(), std::size_t{3});

    const auto& function = require_function(result.program.declarations.front());
    const auto& range =
        require_statement_node<ForRangeStatement>(function.body->items[0]);
    const auto& each =
        require_statement_node<ForEachStatement>(function.body->items[1]);
    const auto& duplicate_range =
        require_statement_node<VariableDeclaration>(range.body->items.front());
    const auto& duplicate_each =
        require_statement_node<VariableDeclaration>(each.body->items.front());

    const auto range_binding =
        require_symbol_id(result.declarations.symbol_for(range));
    const auto each_binding =
        require_symbol_id(result.declarations.symbol_for(each));
    TPP_CHECK(!result.declarations.symbol_for(duplicate_range).has_value());
    TPP_CHECK(!result.declarations.symbol_for(duplicate_each).has_value());
    TPP_CHECK_EQ(
        result.symbols.lookup_local(
            require_scope_id(result.declarations.scope_for(*range.body)),
            "i"),
        range_binding);
    TPP_CHECK_EQ(
        result.symbols.lookup_local(
            require_scope_id(result.declarations.scope_for(*each.body)),
            "item"),
        each_binding);

    constexpr std::array expected_names{
        std::string_view{"i"},
        std::string_view{"item"},
    };
    const auto diagnostics = result.diagnostics.diagnostics();
    for (std::size_t index = 0; index < expected_names.size(); ++index) {
        const auto& error = diagnostics[index * 2];
        const auto& note = diagnostics[index * 2 + 1];
        TPP_CHECK_EQ(error.severity, DiagnosticSeverity::error);
        TPP_CHECK_EQ(note.severity, DiagnosticSeverity::note);
        TPP_CHECK_EQ(
            error.message,
            std::string{"duplicate declaration of '"}
                + std::string{expected_names[index]} + "'");
        TPP_CHECK_EQ(note.message, std::string{"previous declaration is here"});
        TPP_CHECK_EQ(
            result.sources.slice(*error.primary_span),
            expected_names[index]);
        TPP_CHECK_EQ(
            result.sources.slice(*note.primary_span),
            expected_names[index]);
    }
}

void malformed_declared_type_is_diagnosed_without_a_symbol()
{
    TypeContext types;
    SymbolTable symbols;
    DeclarationInfo declarations;
    DiagnosticEngine diagnostics;
    const SourceSpan type_span{SourceId{0}, 0, 7};
    const SourceSpan name_span{SourceId{0}, 8, 13};
    Program program{
        .span = SourceSpan{SourceId{0}, 0, 14},
        .declarations = {},
    };
    program.declarations.emplace_back(VariableDeclaration{
        .span = SourceSpan{SourceId{0}, 0, 14},
        .type = ValueType{
            .span = type_span,
            .node = VectorType{.element_type = nullptr},
        },
        .name = "value",
        .name_span = name_span,
        .initializer = nullptr,
    });
    const auto& declaration = require_variable(program.declarations.front());
    DeclarationCollector collector{types, symbols, declarations, diagnostics};

    TPP_CHECK(!collector.collect(program));
    TPP_CHECK_EQ(diagnostics.error_count(), std::size_t{1});
    TPP_CHECK_EQ(diagnostics.diagnostics().size(), std::size_t{1});
    const auto& diagnostic = diagnostics.diagnostics().front();
    TPP_CHECK_EQ(diagnostic.severity, DiagnosticSeverity::error);
    TPP_CHECK_EQ(diagnostic.message, std::string{"invalid declared type"});
    TPP_CHECK(diagnostic.primary_span.has_value());
    check_span(*diagnostic.primary_span, type_span);
    TPP_CHECK_EQ(symbols.symbol_count(), std::size_t{0});
    TPP_CHECK(!declarations.symbol_for(declaration).has_value());
}

void unknown_names_and_forward_references_are_not_resolved()
{
    CollectionResult result{
        "int first() { return later; }"
        "int later;"
        "void use() {"
        "missing;"
        "call(before);"
        "int before;"
        "void call() { unknown(other); }"
        "}"};

    TPP_CHECK(result.succeeded);
    TPP_CHECK(!result.diagnostics.has_errors());
    TPP_CHECK(result.diagnostics.diagnostics().empty());

    const auto global = result.symbols.global_scope();
    TPP_CHECK(result.symbols.lookup_local(global, "first").has_value());
    TPP_CHECK(result.symbols.lookup_local(global, "later").has_value());
    TPP_CHECK(result.symbols.lookup_local(global, "use").has_value());

    const auto& use = require_function(result.program.declarations[2]);
    const auto use_scope =
        require_scope_id(result.declarations.scope_for(*use.body));
    TPP_CHECK(result.symbols.lookup_local(use_scope, "before").has_value());
    TPP_CHECK(result.symbols.lookup_local(use_scope, "call").has_value());
    TPP_CHECK(!result.symbols.lookup(use_scope, "missing").has_value());
    TPP_CHECK(!result.symbols.lookup(use_scope, "unknown").has_value());
    TPP_CHECK(!result.symbols.lookup(use_scope, "other").has_value());
}

}

int main()
{
    return tpp::test::run({
        {"globals functions parameters and locals",
         globals_functions_parameters_and_locals_are_collected},
        {"nested functions blocks shadowing and siblings",
         nested_functions_blocks_shadowing_and_siblings_form_lexical_scopes},
        {"control flow blocks are child scopes",
         control_flow_blocks_are_independent_child_scopes},
        {"duplicates share namespace and recover",
         duplicate_declarations_share_a_namespace_and_recover},
        {"loop bindings and deferred foreach type",
         loop_bindings_have_body_scope_and_deferred_foreach_type},
        {"loop declarations conflict with bindings",
         loop_body_declarations_conflict_with_their_bindings},
        {"malformed declared type",
         malformed_declared_type_is_diagnosed_without_a_symbol},
        {"unknown names and forward references",
         unknown_names_and_forward_references_are_not_resolved},
    });
}

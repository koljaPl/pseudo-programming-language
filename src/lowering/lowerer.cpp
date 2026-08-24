#include "pseudo/lowering/lowerer.hpp"

#include "pseudo/ast/program.hpp"
#include "pseudo/diagnostics/diagnostic_engine.hpp"
#include "pseudo/semantic/declaration_info.hpp"
#include "pseudo/semantic/resolution_info.hpp"
#include "pseudo/semantic/type_info.hpp"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace tpp {
namespace {

constexpr std::string_view maximum_integer_magnitude =
    "9223372036854775807";
constexpr std::string_view minimum_integer_magnitude =
    "9223372036854775808";

[[nodiscard]] std::optional<std::string> normalize_integer_lexeme(
    const std::string_view lexeme)
{
    if (lexeme.empty()) {
        return std::nullopt;
    }

    for (const char character : lexeme) {
        if (character < '0' || character > '9') {
            return std::nullopt;
        }
    }

    const auto first_non_zero = lexeme.find_first_not_of('0');
    if (first_non_zero == std::string_view::npos) {
        return std::string{"0"};
    }
    return std::string{lexeme.substr(first_non_zero)};
}

[[nodiscard]] bool exceeds_magnitude(
    const std::string_view value,
    const std::string_view maximum) noexcept
{
    return value.size() > maximum.size()
        || (value.size() == maximum.size() && value > maximum);
}

[[nodiscard]] const IntegerLiteralExpression* unwrap_integer_literal(
    const Expression& expression) noexcept
{
    if (const auto* literal =
            std::get_if<IntegerLiteralExpression>(&expression.node)) {
        return literal;
    }
    const auto* parenthesized =
        std::get_if<ParenthesizedExpression>(&expression.node);
    if (parenthesized == nullptr || parenthesized->expression == nullptr) {
        return nullptr;
    }
    return unwrap_integer_literal(*parenthesized->expression);
}

[[nodiscard]] bool is_minimum_integer_value(
    const Expression& expression) noexcept
{
    if (const auto* parenthesized =
            std::get_if<ParenthesizedExpression>(&expression.node)) {
        return parenthesized->expression != nullptr
            && is_minimum_integer_value(*parenthesized->expression);
    }

    const auto* unary = std::get_if<UnaryExpression>(&expression.node);
    if (unary == nullptr || unary->operand == nullptr) {
        return false;
    }
    if (unary->operator_kind == UnaryOperator::plus) {
        return is_minimum_integer_value(*unary->operand);
    }
    if (unary->operator_kind != UnaryOperator::minus) {
        return false;
    }

    const auto* literal = unwrap_integer_literal(*unary->operand);
    if (literal == nullptr) {
        return false;
    }
    const auto magnitude = normalize_integer_lexeme(literal->lexeme);
    return magnitude.has_value()
        && *magnitude == minimum_integer_magnitude;
}

[[nodiscard]] std::string_view builtin_name(
    const BuiltinFunctionKind builtin) noexcept
{
    switch (builtin) {
    case BuiltinFunctionKind::print:
        return "print";
    case BuiltinFunctionKind::read_int:
        return "read_int";
    case BuiltinFunctionKind::read_string:
        return "read_string";
    case BuiltinFunctionKind::read_char:
        return "read_char";
    case BuiltinFunctionKind::len:
        return "len";
    case BuiltinFunctionKind::substring:
        return "substring";
    }
    return "<unknown>";
}

class Lowerer {
public:
    Lowerer(
        const LoweringContext& context,
        DiagnosticEngine& diagnostics)
        : context_{context}
        , diagnostics_{diagnostics}
        , initial_error_count_{diagnostics.error_count()}
    {}

    [[nodiscard]] std::optional<LoweredProgram> lower(
        const Program& program)
    {
        collect_top_level_functions(program);

        LoweredProgram lowered{program.span};
        lowered.functions.reserve(functions_.size());

        for (const auto& function : functions_) {
            auto result = lower_function(function);
            if (result.has_value()) {
                lowered.functions.push_back(std::move(*result));
            }
        }

        if (has_new_errors()) {
            return std::nullopt;
        }
        return lowered;
    }

private:
    struct FunctionEntry {
        const FunctionDeclaration* declaration;
        SymbolId symbol;
        bool is_main;
    };

    struct ActiveStorage {
        TypeId type;
        bool is_parameter;
    };

    [[nodiscard]] bool has_new_errors() const noexcept
    {
        return diagnostics_.error_count() != initial_error_count_;
    }

    void report(const SourceSpan span, std::string message)
    {
        diagnostics_.error(span, std::move(message));
    }

    [[nodiscard]] const Symbol* symbol(
        const SymbolId id,
        const SourceSpan span,
        const std::string_view role)
    {
        if (id.value >= context_.symbols.symbol_count()) {
            report(
                span,
                "malformed semantic state: " + std::string{role}
                    + " has an invalid symbol");
            return nullptr;
        }
        return &context_.symbols.symbol(id);
    }

    [[nodiscard]] bool known_type(
        const TypeId type,
        const SourceSpan span,
        const std::string_view role)
    {
        if (!context_.types.lookup(type).has_value()) {
            report(
                span,
                "malformed semantic state: " + std::string{role}
                    + " has an unknown type");
            return false;
        }
        return true;
    }

    [[nodiscard]] bool valid_value_type(
        const TypeId type,
        const SourceSpan span,
        const std::string_view role)
    {
        const auto descriptor = context_.types.lookup(type);
        if (!descriptor.has_value()) {
            report(
                span,
                "malformed semantic state: " + std::string{role}
                    + " has an unknown type");
            return false;
        }
        if (const auto* primitive =
                std::get_if<PrimitiveTypeKind>(&*descriptor)) {
            if (*primitive == PrimitiveTypeKind::void_type) {
                report(
                    span,
                    "malformed semantic state: " + std::string{role}
                        + " has type 'void'");
                return false;
            }
            return true;
        }

        const auto element = std::get<SemanticVectorType>(*descriptor);
        return valid_value_type(element.element_type, span, "vector element");
    }

    [[nodiscard]] std::optional<TypeId> scalar_type(
        const ScalarTypeKind kind) const noexcept
    {
        switch (kind) {
        case ScalarTypeKind::integer:
            return context_.types.integer_type();
        case ScalarTypeKind::boolean:
            return context_.types.boolean_type();
        case ScalarTypeKind::character:
            return context_.types.character_type();
        case ScalarTypeKind::string:
            return context_.types.string_type();
        }
        return std::nullopt;
    }

    [[nodiscard]] bool syntax_type_matches(
        const ValueType& syntax,
        const TypeId semantic) const noexcept
    {
        return std::visit(
            [this, semantic](const auto& node) {
                using Node = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<Node, ScalarTypeKind>) {
                    const auto type = scalar_type(node);
                    return type.has_value() && *type == semantic;
                } else {
                    const auto descriptor = context_.types.lookup(semantic);
                    const auto* vector = descriptor.has_value()
                        ? std::get_if<SemanticVectorType>(&*descriptor)
                        : nullptr;
                    return vector != nullptr && node.element_type != nullptr
                        && syntax_type_matches(
                            *node.element_type,
                            vector->element_type);
                }
            },
            syntax.node);
    }

    [[nodiscard]] bool syntax_return_type_matches(
        const ReturnType& syntax,
        const TypeId semantic) const noexcept
    {
        return std::visit(
            [this, semantic](const auto& node) {
                using Node = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<Node, VoidType>) {
                    return semantic == context_.types.void_type();
                } else {
                    return syntax_type_matches(node, semantic);
                }
            },
            syntax.node);
    }

    [[nodiscard]] std::optional<TypeId> semantic_primitive_type(
        const PrimitiveTypeKind kind) const noexcept
    {
        switch (kind) {
        case PrimitiveTypeKind::integer:
            return context_.types.integer_type();
        case PrimitiveTypeKind::boolean:
            return context_.types.boolean_type();
        case PrimitiveTypeKind::character:
            return context_.types.character_type();
        case PrimitiveTypeKind::string:
            return context_.types.string_type();
        case PrimitiveTypeKind::void_type:
            return context_.types.void_type();
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<PrimitiveTypeKind> primitive_kind(
        const TypeId type) const noexcept
    {
        const auto descriptor = context_.types.lookup(type);
        if (!descriptor.has_value()) {
            return std::nullopt;
        }
        const auto* primitive = std::get_if<PrimitiveTypeKind>(&*descriptor);
        return primitive != nullptr
            ? std::optional<PrimitiveTypeKind>{*primitive}
            : std::nullopt;
    }

    [[nodiscard]] bool is_vector_type(const TypeId type) const noexcept
    {
        const auto descriptor = context_.types.lookup(type);
        return descriptor.has_value()
            && std::holds_alternative<SemanticVectorType>(*descriptor);
    }

    [[nodiscard]] bool is_supported_local_type(
        const TypeId type) const noexcept
    {
        return type == context_.types.integer_type()
            || type == context_.types.boolean_type()
            || type == context_.types.string_type()
            || type == context_.types.character_type()
            || is_vector_type(type);
    }

    void collect_top_level_functions(const Program& program)
    {
        const FunctionDeclaration* main_declaration = nullptr;

        for (const auto& declaration : program.declarations) {
            std::visit(
                [this, &main_declaration](const auto& node) {
                    using Node = std::decay_t<decltype(node)>;
                    if constexpr (std::is_same_v<Node, VariableDeclaration>) {
                        report(
                            node.span,
                            "C++ code generation does not support global "
                            "variables yet");
                    } else {
                        const auto is_main = node.name == "main";
                        if (is_main && main_declaration != nullptr) {
                            report(
                                node.name_span,
                                "C++ code generation requires exactly one "
                                "top-level 'main' function");
                            return;
                        }
                        if (is_main) {
                            main_declaration = &node;
                        }
                        validate_top_level_function(node, is_main);
                    }
                },
                declaration);
        }

        if (main_declaration == nullptr) {
            report(
                program.span,
                "C++ code generation requires a top-level 'int main()' "
                "function");
        }
    }

    void validate_top_level_function(
        const FunctionDeclaration& declaration,
        const bool is_main)
    {
        const auto function_id =
            context_.declarations.symbol_for(declaration);
        if (!function_id.has_value()) {
            report(
                declaration.name_span,
                "malformed semantic state: function declaration has no "
                "symbol");
            return;
        }
        const auto* entry = symbol(
            *function_id,
            declaration.name_span,
            "function declaration");
        if (entry == nullptr) {
            return;
        }
        const auto* function = std::get_if<FunctionSymbol>(&entry->data);
        if (function == nullptr) {
            report(
                declaration.name_span,
                "malformed semantic state: function declaration symbol is "
                "not a function");
            return;
        }
        if (!syntax_return_type_matches(
                declaration.return_type,
                function->return_type)) {
            report(
                declaration.return_type.span,
                "malformed semantic state: function return type does not "
                "match its declaration");
        }
        (void)known_type(
            function->return_type,
            declaration.return_type.span,
            "function return type");

        if (!top_level_function_ids_.insert(function_id->value).second) {
            report(
                declaration.name_span,
                "malformed semantic state: top-level function symbol is "
                "reused");
        }

        functions_.push_back(FunctionEntry{
            .declaration = &declaration,
            .symbol = *function_id,
            .is_main = is_main,
        });

        if (is_main) {
            main_symbol_ = *function_id;
            if (function->return_type != context_.types.integer_type()
                || !function->parameter_types.empty()
                || !declaration.parameters.empty()) {
                report(
                    declaration.name_span,
                    "C++ code generation requires 'main' to have return type "
                    "'int' and no parameters");
            }
        }

        if (declaration.parameters.size()
            != function->parameter_types.size()) {
            report(
                declaration.name_span,
                "malformed semantic state: function parameter signature "
                "does not match its declaration");
        }

        const auto parameter_count = std::min(
            declaration.parameters.size(),
            function->parameter_types.size());
        for (std::size_t index = 0; index < parameter_count; ++index) {
            validate_parameter(
                declaration.parameters[index],
                function->parameter_types[index]);
        }

        if (declaration.body == nullptr) {
            report(
                declaration.span,
                "malformed AST: function '" + declaration.name
                    + "' has no body");
        }
    }

    void validate_parameter(
        const Parameter& parameter,
        const TypeId signature_type)
    {
        const auto parameter_id = context_.declarations.symbol_for(parameter);
        if (!parameter_id.has_value()) {
            report(
                parameter.name_span,
                "malformed semantic state: parameter declaration has no "
                "symbol");
            return;
        }
        const auto* entry = symbol(
            *parameter_id,
            parameter.name_span,
            "parameter declaration");
        if (entry == nullptr) {
            return;
        }
        const auto* data = std::get_if<ParameterSymbol>(&entry->data);
        if (data == nullptr) {
            report(
                parameter.name_span,
                "malformed semantic state: parameter declaration symbol is "
                "not a parameter");
            return;
        }
        if (data->type != signature_type) {
            report(
                parameter.name_span,
                "malformed semantic state: parameter type does not match "
                "function signature");
            return;
        }
        if (!syntax_type_matches(parameter.type, signature_type)) {
            report(
                parameter.type.span,
                "malformed semantic state: parameter semantic type does not "
                "match its declaration");
            return;
        }
        (void)valid_value_type(signature_type, parameter.type.span, "parameter");
    }

    [[nodiscard]] const FunctionSymbol* function_symbol(
        const FunctionEntry& entry)
    {
        const auto* symbol_entry = symbol(
            entry.symbol,
            entry.declaration->name_span,
            "function declaration");
        if (symbol_entry == nullptr) {
            return nullptr;
        }
        const auto* function =
            std::get_if<FunctionSymbol>(&symbol_entry->data);
        if (function == nullptr) {
            report(
                entry.declaration->name_span,
                "malformed semantic state: function declaration symbol is "
                "not a function");
        }
        return function;
    }

    [[nodiscard]] std::optional<LoweredFunction> lower_function(
        const FunctionEntry& entry)
    {
        const auto* function = function_symbol(entry);
        if (function == nullptr || entry.declaration->body == nullptr) {
            return std::nullopt;
        }

        current_function_ = &entry;
        current_storages_.clear();
        loop_depth_ = 0;

        std::vector<LoweredParameter> parameters;
        const auto count = std::min(
            entry.declaration->parameters.size(),
            function->parameter_types.size());
        parameters.reserve(count);
        for (std::size_t index = 0; index < count; ++index) {
            const auto& parameter = entry.declaration->parameters[index];
            const auto parameter_id =
                context_.declarations.symbol_for(parameter);
            if (!parameter_id.has_value()) {
                continue;
            }
            if (!current_storages_.emplace(
                    parameter_id->value,
                    ActiveStorage{
                        .type = function->parameter_types[index],
                        .is_parameter = true,
                    }).second) {
                report(
                    parameter.name_span,
                    "malformed semantic state: parameter symbol is already "
                    "active");
                continue;
            }
            parameters.push_back(LoweredParameter{
                .span = parameter.span,
                .name_span = parameter.name_span,
                .symbol = *parameter_id,
                .type = function->parameter_types[index],
            });
        }

        auto body = lower_block(*entry.declaration->body);
        LoweredFunction result{
            .span = entry.declaration->span,
            .name_span = entry.declaration->name_span,
            .symbol = entry.symbol,
            .return_type = function->return_type,
            .parameters = std::move(parameters),
            .body = std::move(body),
            .is_main = entry.is_main,
        };

        current_storages_.clear();
        loop_depth_ = 0;
        current_function_ = nullptr;
        if (result.body == nullptr) {
            return std::nullopt;
        }
        return result;
    }

    [[nodiscard]] LoweredBlockPtr lower_block(const Block& block)
    {
        auto lowered = std::make_unique<LoweredBlock>(LoweredBlock{
            .span = block.span,
            .statements = {},
        });
        lowered->statements.reserve(block.items.size());

        const auto errors_before = diagnostics_.error_count();
        for (const auto& item : block.items) {
            std::visit(
                [this, &lowered](const auto& node) {
                    using Node = std::decay_t<decltype(node)>;
                    if constexpr (std::is_same_v<Node, FunctionDeclaration>) {
                        report(
                            node.span,
                            "C++ code generation does not support nested "
                            "functions yet");
                    } else {
                        auto statement = lower_statement(node);
                        if (statement.has_value()) {
                            lowered->statements.push_back(
                                std::move(*statement));
                        }
                    }
                },
                item);
        }

        if (diagnostics_.error_count() != errors_before) {
            return nullptr;
        }
        return lowered;
    }

    [[nodiscard]] LoweredBlockPtr lower_scoped_block(const Block& block)
    {
        auto enclosing = current_storages_;
        auto lowered = lower_block(block);
        current_storages_ = std::move(enclosing);
        return lowered;
    }

    [[nodiscard]] std::optional<LoweredStatement> lower_statement(
        const Statement& statement)
    {
        auto node = std::visit(
            [this, span = statement.span](const auto& value)
                -> std::optional<LoweredStatementNode> {
                return lower_statement_node(span, value);
            },
            statement.node);
        if (!node.has_value()) {
            return std::nullopt;
        }
        return LoweredStatement{
            .span = statement.span,
            .node = std::move(*node),
        };
    }

    [[nodiscard]] std::optional<LoweredStatementNode> lower_statement_node(
        SourceSpan span,
        const VariableDeclaration& declaration);
    [[nodiscard]] std::optional<LoweredStatementNode> lower_statement_node(
        SourceSpan span,
        const AssignmentStatement& statement);
    [[nodiscard]] std::optional<LoweredStatementNode> lower_statement_node(
        SourceSpan span,
        const ExpressionStatement& statement);
    [[nodiscard]] std::optional<LoweredStatementNode> lower_statement_node(
        SourceSpan span,
        const IfStatement&);
    [[nodiscard]] std::optional<LoweredStatementNode> lower_statement_node(
        SourceSpan span,
        const WhileStatement&);
    [[nodiscard]] std::optional<LoweredStatementNode> lower_statement_node(
        SourceSpan span,
        const ForRangeStatement& statement);
    [[nodiscard]] std::optional<LoweredStatementNode> lower_statement_node(
        SourceSpan span,
        const ForEachStatement& statement);
    [[nodiscard]] std::optional<LoweredStatementNode> lower_statement_node(
        SourceSpan span,
        const ReturnStatement& statement);
    [[nodiscard]] std::optional<LoweredStatementNode> lower_statement_node(
        SourceSpan span,
        const BreakStatement&);
    [[nodiscard]] std::optional<LoweredStatementNode> lower_statement_node(
        SourceSpan span,
        const ContinueStatement&);
    [[nodiscard]] std::optional<LoweredStatementNode> lower_statement_node(
        SourceSpan span,
        const BlockStatement& statement);

    [[nodiscard]] std::optional<ActiveStorage> active_storage(
        SymbolId id,
        SourceSpan span,
        std::string_view role);

    [[nodiscard]] std::optional<TypeId> expression_type(
        const Expression& expression);
    [[nodiscard]] LoweredExpressionPtr lower_expression(
        const Expression& expression,
        bool allow_minimum_magnitude = false);
    [[nodiscard]] std::optional<LoweredExpressionNode>
    lower_expression_node(
        SourceSpan span,
        TypeId result_type,
        const IntegerLiteralExpression& expression,
        bool allow_minimum_magnitude);
    [[nodiscard]] std::optional<LoweredExpressionNode>
    lower_expression_node(
        SourceSpan span,
        TypeId result_type,
        const BooleanLiteralExpression& expression);
    [[nodiscard]] std::optional<LoweredExpressionNode>
    lower_expression_node(
        SourceSpan span,
        TypeId result_type,
        const CharacterLiteralExpression& expression);
    [[nodiscard]] std::optional<LoweredExpressionNode>
    lower_expression_node(
        SourceSpan span,
        TypeId result_type,
        const StringLiteralExpression& expression);
    [[nodiscard]] std::optional<LoweredExpressionNode>
    lower_expression_node(
        SourceSpan span,
        TypeId result_type,
        const IdentifierExpression& expression);
    [[nodiscard]] std::optional<LoweredExpressionNode>
    lower_expression_node(
        SourceSpan span,
        TypeId result_type,
        const UnaryExpression& expression);
    [[nodiscard]] std::optional<LoweredExpressionNode>
    lower_expression_node(
        SourceSpan span,
        TypeId result_type,
        const BinaryExpression& expression);
    [[nodiscard]] std::optional<LoweredExpressionNode>
    lower_expression_node(
        SourceSpan span,
        TypeId result_type,
        const CallExpression& expression);
    [[nodiscard]] std::optional<LoweredExpressionNode>
    lower_expression_node(
        SourceSpan span,
        TypeId result_type,
        const IndexExpression& expression);
    [[nodiscard]] std::optional<LoweredExpressionNode>
    lower_expression_node(
        SourceSpan span,
        TypeId result_type,
        const MemberAccessExpression&);
    [[nodiscard]] std::optional<LoweredExpressionNode>
    lower_expression_node(
        SourceSpan span,
        TypeId result_type,
        const VectorConstructionExpression& expression);
    [[nodiscard]] std::optional<LoweredExpressionNode>
    lower_expression_node(
        SourceSpan span,
        TypeId result_type,
        const ParenthesizedExpression& expression,
        bool allow_minimum_magnitude);

    [[nodiscard]] std::optional<std::vector<LoweredExpressionPtr>>
    lower_arguments(
        SourceSpan call_span,
        const std::vector<ExpressionPtr>& arguments);
    [[nodiscard]] std::optional<LoweredExpressionNode> lower_builtin_call(
        SourceSpan span,
        TypeId result_type,
        const CallExpression& call,
        const IdentifierExpression& callee,
        BuiltinFunctionKind builtin);
    [[nodiscard]] std::optional<LoweredExpressionNode> lower_user_call(
        SourceSpan span,
        TypeId result_type,
        const CallExpression& call,
        const IdentifierExpression& callee,
        SymbolId function_id);
    [[nodiscard]] std::optional<LoweredExpressionNode> lower_member_call(
        SourceSpan span,
        TypeId result_type,
        const CallExpression& call,
        const MemberAccessExpression& member,
        SourceSpan member_span);

    [[nodiscard]] const IdentifierExpression* unwrap_callable_identifier(
        const Expression& expression) const;
    [[nodiscard]] const MemberAccessExpression* unwrap_member_access(
        const Expression& expression) const;
    [[nodiscard]] const CallExpression* unwrap_call(
        const Expression& expression) const;

    [[nodiscard]] TempId next_temp() noexcept
    {
        const TempId id{next_temp_id_};
        ++next_temp_id_;
        return id;
    }

    [[nodiscard]] LoweredTemporary make_temp(
        TempRole role,
        SymbolId owner,
        TypeId type,
        SourceSpan span)
    {
        return LoweredTemporary{
            .id = next_temp(),
            .role = role,
            .owner = owner,
            .type = type,
            .span = span,
        };
    }

    const LoweringContext& context_;
    DiagnosticEngine& diagnostics_;

    std::size_t initial_error_count_;
    std::vector<FunctionEntry> functions_;
    std::unordered_set<std::size_t> top_level_function_ids_;
    std::optional<SymbolId> main_symbol_;

    const FunctionEntry* current_function_{nullptr};

    std::unordered_map<std::size_t, ActiveStorage> current_storages_;
    std::size_t loop_depth_{0};
    std::size_t next_temp_id_{0};

    const CallExpression* expression_statement_call_{nullptr};
};

std::optional<Lowerer::ActiveStorage> Lowerer::active_storage(
    const SymbolId id,
    const SourceSpan span,
    const std::string_view role)
{
    const auto* entry = symbol(id, span, role);
    if (entry == nullptr) {
        return std::nullopt;
    }

    const auto active = current_storages_.find(id.value);
    if (const auto* parameter = std::get_if<ParameterSymbol>(&entry->data)) {
        if (active == current_storages_.end()
            || !active->second.is_parameter) {
            report(
                span,
                "malformed semantic state: parameter reference is outside "
                "its function");
            return std::nullopt;
        }
        if (active->second.type != parameter->type) {
            report(
                span,
                "malformed semantic state: active parameter type does not "
                "match its symbol");
            return std::nullopt;
        }
        return active->second;
    }

    if (const auto* variable = std::get_if<VariableSymbol>(&entry->data)) {
        if (active == current_storages_.end()
            || active->second.is_parameter) {
            report(
                span,
                "C++ code generation only supports references to local int, "
                "string, char, or vector variables in the current function "
                "yet");
            return std::nullopt;
        }
        if (variable->type.has_value()
            && *variable->type != active->second.type) {
            report(
                span,
                "malformed semantic state: active local variable type does "
                "not match its symbol");
            return std::nullopt;
        }
        return active->second;
    }

    report(
        span,
        "malformed semantic state: " + std::string{role}
            + " is not a variable or parameter");
    
    return std::nullopt;
}

std::optional<LoweredStatementNode> Lowerer::lower_statement_node(
    const SourceSpan span,
    const VariableDeclaration& declaration)
{
    const auto declaration_id =
        context_.declarations.symbol_for(declaration);
    if (!declaration_id.has_value()) {
        report(
            declaration.name_span,
            "malformed semantic state: local variable declaration has no "
            "symbol");
        return std::nullopt;
    }
    const auto* entry = symbol(
        *declaration_id,
        declaration.name_span,
        "local variable declaration");
    if (entry == nullptr) {
        return std::nullopt;
    }
    const auto* variable = std::get_if<VariableSymbol>(&entry->data);
    if (variable == nullptr) {
        report(
            declaration.name_span,
            "malformed semantic state: local variable declaration symbol is "
            "not a variable");
        return std::nullopt;
    }
    if (!variable->type.has_value()) {
        report(
            declaration.name_span,
            "malformed semantic state: local variable has no declared type");
        return std::nullopt;
    }
    if (!syntax_type_matches(declaration.type, *variable->type)) {
        report(
            declaration.type.span,
            "malformed semantic state: local variable semantic type does not "
            "match its declaration");
        return std::nullopt;
    }
    if (!is_supported_local_type(*variable->type)) {
        report(
            declaration.type.span,
            "C++ code generation only supports local variables of type "
            "'int', 'bool', 'string', 'char', or 'vector<T>' yet");
        return std::nullopt;
    }
    if (!valid_value_type(
            *variable->type,
            declaration.type.span,
            "local variable")) {
        return std::nullopt;
    }
    if (declaration.initializer == nullptr) {
        report(
            span,
            "C++ code generation only supports initialized local int, bool, "
            "string, char, or vector variables yet");
        return std::nullopt;
    }

    const auto initializer_type = expression_type(*declaration.initializer);
    if (!initializer_type.has_value()) {
        return std::nullopt;
    }
    if (*initializer_type != *variable->type) {
        report(
            declaration.initializer->span,
            "malformed semantic state: local variable initializer type does "
            "not match its declaration");
        return std::nullopt;
    }
    if (current_storages_.contains(declaration_id->value)) {
        report(
            declaration.name_span,
            "malformed semantic state: local variable declaration symbol is "
            "already active");
        return std::nullopt;
    }

    auto initializer = lower_expression(*declaration.initializer);
    if (initializer == nullptr) {
        return std::nullopt;
    }
    current_storages_.emplace(
        declaration_id->value,
        ActiveStorage{
            .type = *variable->type,
            .is_parameter = false,
        });
    return LoweredStatementNode{LoweredVariableStatement{
        .symbol = *declaration_id,
        .name_span = declaration.name_span,
        .type = *variable->type,
        .initializer = std::move(initializer),
    }};
}

std::optional<LoweredStatementNode> Lowerer::lower_statement_node(
    const SourceSpan span,
    const AssignmentStatement& statement)
{
    if (statement.value == nullptr) {
        report(span, "malformed AST: assignment has no value");
        return std::nullopt;
    }

    const auto resolution =
        context_.resolutions.resolution_for(statement.target);
    if (!resolution.has_value()) {
        report(
            statement.target.name_span,
            "malformed semantic state: assignment target has no resolution");
        return std::nullopt;
    }
    const auto* target_id = std::get_if<SymbolId>(&*resolution);
    if (target_id == nullptr) {
        report(
            statement.target.name_span,
            "malformed semantic state: assignment target resolves to a "
            "builtin");
        return std::nullopt;
    }
    const auto storage = active_storage(
        *target_id,
        statement.target.name_span,
        "assignment target");
    if (!storage.has_value()) {
        return std::nullopt;
    }

    const auto target_type = context_.type_info.type_of(statement.target);
    if (!target_type.has_value()) {
        report(
            statement.target.span,
            "malformed semantic state: assignment target has no type");
        return std::nullopt;
    }
    if (!known_type(*target_type, statement.target.span, "assignment target")) {
        return std::nullopt;
    }

    auto current_type = storage->type;
    std::vector<TypeId> container_types;
    std::vector<LoweredExpressionPtr> indices;
    container_types.reserve(statement.target.indices.size());
    indices.reserve(statement.target.indices.size());
    for (const auto& index : statement.target.indices) {
        if (index == nullptr) {
            report(
                statement.target.span,
                "malformed AST: assignment target index is missing");
            return std::nullopt;
        }
        const auto index_type = expression_type(*index);
        if (!index_type.has_value()
            || *index_type != context_.types.integer_type()) {
            if (index_type.has_value()) {
                report(
                    index->span,
                    "malformed semantic state: assignment target index does "
                    "not have type 'int'");
            }
            return std::nullopt;
        }

        container_types.push_back(current_type);
        if (current_type == context_.types.string_type()) {
            current_type = context_.types.character_type();
        } else {
            const auto descriptor = context_.types.lookup(current_type);
            const auto* vector = descriptor.has_value()
                ? std::get_if<SemanticVectorType>(&*descriptor)
                : nullptr;
            if (vector == nullptr) {
                report(
                    statement.target.span,
                    "malformed semantic state: assignment target indexes a "
                    "non-indexable type");
                return std::nullopt;
            }
            current_type = vector->element_type;
        }

        auto lowered_index = lower_expression(*index);
        if (lowered_index == nullptr) {
            return std::nullopt;
        }
        indices.push_back(std::move(lowered_index));
    }
    if (current_type != *target_type) {
        report(
            statement.target.span,
            "malformed semantic state: assignment target type does not match "
            "its indexed symbol type");
        return std::nullopt;
    }

    const auto value_type = expression_type(*statement.value);
    if (!value_type.has_value()) {
        return std::nullopt;
    }
    if (*value_type != *target_type) {
        report(
            statement.value->span,
            "malformed semantic state: assignment value type does not match "
            "its target");
        return std::nullopt;
    }

    auto operator_kind = LoweredAssignmentOperator::assign;
    switch (statement.operator_kind) {
    case AssignmentOperator::assign:
        operator_kind = LoweredAssignmentOperator::assign;
        break;
    case AssignmentOperator::add_assign:
        operator_kind = LoweredAssignmentOperator::add_assign;
        break;
    case AssignmentOperator::subtract_assign:
        operator_kind = LoweredAssignmentOperator::subtract_assign;
        break;
    case AssignmentOperator::multiply_assign:
        operator_kind = LoweredAssignmentOperator::multiply_assign;
        break;
    case AssignmentOperator::divide_assign:
        operator_kind = LoweredAssignmentOperator::divide_assign;
        break;
    case AssignmentOperator::remainder_assign:
        operator_kind = LoweredAssignmentOperator::remainder_assign;
        break;
    default:
        report(span, "malformed AST: unknown assignment operator");
        return std::nullopt;
    }

    const auto direct_assignment =
        statement.operator_kind == AssignmentOperator::assign;
    const auto integer_compound = !direct_assignment
        && *target_type == context_.types.integer_type();
    const auto string_addition =
        statement.operator_kind == AssignmentOperator::add_assign
        && *target_type == context_.types.string_type();
    if (!direct_assignment && !integer_compound && !string_addition) {
        report(
            span,
            "malformed semantic state: assignment operator is incompatible "
            "with its target type");
        return std::nullopt;
    }

    auto value = lower_expression(*statement.value);
    if (value == nullptr) {
        return std::nullopt;
    }
    return LoweredStatementNode{LoweredAssignmentStatement{
        .target = LoweredAssignmentTarget{
            .span = statement.target.span,
            .storage_span = statement.target.name_span,
            .storage = *target_id,
            .storage_type = storage->type,
            .type = *target_type,
            .container_types = std::move(container_types),
            .indices = std::move(indices),
        },
        .operator_kind = operator_kind,
        .value = std::move(value),
    }};
}

std::optional<LoweredStatementNode> Lowerer::lower_statement_node(
    const SourceSpan span,
    const ExpressionStatement& statement)
{
    if (statement.expression == nullptr) {
        report(span, "malformed AST: expression statement has no expression");
        return std::nullopt;
    }
    const auto* call = unwrap_call(*statement.expression);
    if (call == nullptr) {
        report(
            statement.expression->span,
            "C++ code generation only supports function calls as expression "
            "statements");
        return std::nullopt;
    }

    expression_statement_call_ = call;
    auto expression = lower_expression(*statement.expression);
    expression_statement_call_ = nullptr;
    if (expression == nullptr) {
        return std::nullopt;
    }
    return LoweredStatementNode{LoweredExpressionStatement{
        .expression = std::move(expression),
    }};
}

std::optional<LoweredStatementNode> Lowerer::lower_statement_node(
    const SourceSpan span,
    const IfStatement& statement)
{
    if (statement.condition == nullptr) {
        report(span, "malformed AST: if statement is missing a condition");
        return std::nullopt;
    }
    if (statement.then_block == nullptr) {
        report(span, "malformed AST: if statement is missing a then block");
        return std::nullopt;
    }

    const auto condition_type = expression_type(*statement.condition);
    if (!condition_type.has_value()) {
        return std::nullopt;
    }
    if (*condition_type != context_.types.boolean_type()) {
        report(
            statement.condition->span,
            "malformed semantic state: if condition does not have type "
            "'bool'");
        return std::nullopt;
    }

    auto condition = lower_expression(*statement.condition);
    auto then_block = lower_scoped_block(*statement.then_block);
    LoweredBlockPtr else_block;
    if (statement.else_block != nullptr) {
        else_block = lower_scoped_block(*statement.else_block);
    }
    if (condition == nullptr || then_block == nullptr
        || (statement.else_block != nullptr && else_block == nullptr)) {
        return std::nullopt;
    }

    return LoweredStatementNode{LoweredIfStatement{
        .condition = std::move(condition),
        .then_block = std::move(then_block),
        .else_block = std::move(else_block),
    }};
}

std::optional<LoweredStatementNode> Lowerer::lower_statement_node(
    const SourceSpan span,
    const WhileStatement& statement)
{
    if (statement.condition == nullptr) {
        report(span, "malformed AST: while statement is missing a condition");
        return std::nullopt;
    }
    if (statement.body == nullptr) {
        report(span, "malformed AST: while statement is missing a body");
        return std::nullopt;
    }

    const auto condition_type = expression_type(*statement.condition);
    if (!condition_type.has_value()) {
        return std::nullopt;
    }
    if (*condition_type != context_.types.boolean_type()) {
        report(
            statement.condition->span,
            "malformed semantic state: while condition does not have type "
            "'bool'");
        return std::nullopt;
    }

    auto condition = lower_expression(*statement.condition);
    ++loop_depth_;
    auto body = lower_scoped_block(*statement.body);
    --loop_depth_;
    if (condition == nullptr || body == nullptr) {
        return std::nullopt;
    }

    return LoweredStatementNode{LoweredWhileStatement{
        .condition = std::move(condition),
        .body = std::move(body),
    }};
}

std::optional<LoweredStatementNode> Lowerer::lower_statement_node(
    const SourceSpan span,
    const ForRangeStatement& statement)
{
    if (statement.begin == nullptr || statement.end == nullptr) {
        report(span, "malformed AST: for-range statement is missing a bound");
        return std::nullopt;
    }
    if (statement.body == nullptr) {
        report(span, "malformed AST: for-range statement is missing a body");
        return std::nullopt;
    }

    const auto binding_id = context_.declarations.symbol_for(statement);
    if (!binding_id.has_value()) {
        report(
            statement.variable_span,
            "malformed semantic state: for-range binding has no symbol");
        return std::nullopt;
    }
    const auto* entry = symbol(
        *binding_id,
        statement.variable_span,
        "for-range binding");
    if (entry == nullptr) {
        return std::nullopt;
    }
    const auto* variable = std::get_if<VariableSymbol>(&entry->data);
    if (variable == nullptr) {
        report(
            statement.variable_span,
            "malformed semantic state: for-range binding symbol is not a "
            "variable");
        return std::nullopt;
    }
    if (!variable->type.has_value()
        || *variable->type != context_.types.integer_type()) {
        report(
            statement.variable_span,
            "malformed semantic state: for-range binding does not have type "
            "'int'");
        return std::nullopt;
    }
    if (current_storages_.contains(binding_id->value)) {
        report(
            statement.variable_span,
            "malformed semantic state: for-range binding is already active");
        return std::nullopt;
    }

    const auto begin_type = expression_type(*statement.begin);
    const auto end_type = expression_type(*statement.end);
    if (!begin_type.has_value()
        || *begin_type != context_.types.integer_type()) {
        if (begin_type.has_value()) {
            report(
                statement.begin->span,
                "malformed semantic state: for-range begin bound does not "
                "have type 'int'");
        }
        return std::nullopt;
    }
    if (!end_type.has_value()
        || *end_type != context_.types.integer_type()) {
        if (end_type.has_value()) {
            report(
                statement.end->span,
                "malformed semantic state: for-range end bound does not have "
                "type 'int'");
        }
        return std::nullopt;
    }

    auto condition_kind = LoweredRangeConditionKind::cursor_less_than_end;
    auto step_kind = LoweredRangeStepKind::increment_cursor;
    auto inclusive = false;
    switch (statement.operator_kind) {
    case RangeOperator::exclusive:
        break;
    case RangeOperator::inclusive:
        inclusive = true;
        condition_kind = LoweredRangeConditionKind::active;
        step_kind =
            LoweredRangeStepKind::update_active_then_guarded_increment;
        break;
    default:
        report(span, "malformed AST: unknown range operator");
        return std::nullopt;
    }

    auto begin_value = lower_expression(*statement.begin);
    if (begin_value == nullptr) {
        return std::nullopt;
    }
    auto end_value = lower_expression(*statement.end);
    if (end_value == nullptr) {
        return std::nullopt;
    }

    auto begin_storage = make_temp(
        TempRole::range_begin,
        *binding_id,
        context_.types.integer_type(),
        statement.begin->span);
    auto end_storage = make_temp(
        TempRole::range_end,
        *binding_id,
        context_.types.integer_type(),
        statement.end->span);
    auto cursor_storage = make_temp(
        TempRole::range_cursor,
        *binding_id,
        context_.types.integer_type(),
        statement.variable_span);
    auto active_storage = std::optional<LoweredTemporary>{};
    if (inclusive) {
        active_storage = make_temp(
            TempRole::range_active,
            *binding_id,
            context_.types.boolean_type(),
            span);
    }

    current_storages_.emplace(
        binding_id->value,
        ActiveStorage{
            .type = context_.types.integer_type(),
            .is_parameter = false,
        });
    ++loop_depth_;
    auto body = lower_scoped_block(*statement.body);
    --loop_depth_;
    current_storages_.erase(binding_id->value);
    if (body == nullptr) {
        return std::nullopt;
    }

    return LoweredStatementNode{LoweredRangeStatement{
        .binding = *binding_id,
        .binding_span = statement.variable_span,
        .binding_type = context_.types.integer_type(),
        .begin_storage = begin_storage,
        .begin_value = std::move(begin_value),
        .end_storage = end_storage,
        .end_value = std::move(end_value),
        .cursor_storage = cursor_storage,
        .active_storage = active_storage,
        .condition_kind = condition_kind,
        .step_kind = step_kind,
        .body = std::move(body),
    }};
}

std::optional<LoweredStatementNode> Lowerer::lower_statement_node(
    const SourceSpan span,
    const ForEachStatement& statement)
{
    if (statement.iterable == nullptr) {
        report(
            span,
            "malformed AST: for-each statement is missing an iterable");
        return std::nullopt;
    }
    if (statement.body == nullptr) {
        report(span, "malformed AST: for-each statement is missing a body");
        return std::nullopt;
    }

    const auto binding_id = context_.declarations.symbol_for(statement);
    if (!binding_id.has_value()) {
        report(
            statement.variable_span,
            "malformed semantic state: for-each binding has no symbol");
        return std::nullopt;
    }
    const auto* entry = symbol(
        *binding_id,
        statement.variable_span,
        "for-each binding");
    if (entry == nullptr) {
        return std::nullopt;
    }
    const auto* variable = std::get_if<VariableSymbol>(&entry->data);
    if (variable == nullptr) {
        report(
            statement.variable_span,
            "malformed semantic state: for-each binding symbol is not a "
            "variable");
        return std::nullopt;
    }
    if (variable->type.has_value()) {
        report(
            statement.variable_span,
            "malformed semantic state: for-each binding unexpectedly has a "
            "declared type");
        return std::nullopt;
    }
    if (current_storages_.contains(binding_id->value)) {
        report(
            statement.variable_span,
            "malformed semantic state: for-each binding is already active");
        return std::nullopt;
    }

    const auto iterable_type = expression_type(*statement.iterable);
    if (!iterable_type.has_value()) {
        report(
            statement.iterable->span,
            "malformed semantic state: for-each iterable has no type");
        return std::nullopt;
    }
    const auto inferred_type = context_.type_info.inferred_type(*binding_id);
    if (!inferred_type.has_value()) {
        report(
            statement.variable_span,
            "malformed semantic state: for-each binding has no inferred "
            "type");
        return std::nullopt;
    }

    auto expected_binding_type = std::optional<TypeId>{};
    if (*iterable_type == context_.types.string_type()) {
        expected_binding_type = context_.types.character_type();
    } else {
        const auto descriptor = context_.types.lookup(*iterable_type);
        const auto* vector = descriptor.has_value()
            ? std::get_if<SemanticVectorType>(&*descriptor)
            : nullptr;
        if (vector == nullptr) {
            report(
                statement.iterable->span,
                "malformed semantic state: for-each iterable is not a string "
                "or vector");
            return std::nullopt;
        }
        expected_binding_type = vector->element_type;
    }
    if (*inferred_type != *expected_binding_type) {
        report(
            statement.variable_span,
            "malformed semantic state: for-each inferred binding type does "
            "not match its iterable");
        return std::nullopt;
    }
    if (!valid_value_type(
            *iterable_type,
            statement.iterable->span,
            "for-each iterable")
        || !valid_value_type(
            *inferred_type,
            statement.variable_span,
            "for-each binding")) {
        return std::nullopt;
    }

    auto iterable = lower_expression(*statement.iterable);
    if (iterable == nullptr) {
        return std::nullopt;
    }
    auto snapshot_storage = make_temp(
        TempRole::iterable_snapshot,
        *binding_id,
        *iterable_type,
        statement.iterable->span);

    current_storages_.emplace(
        binding_id->value,
        ActiveStorage{
            .type = *inferred_type,
            .is_parameter = false,
        });
    ++loop_depth_;
    auto body = lower_scoped_block(*statement.body);
    --loop_depth_;
    current_storages_.erase(binding_id->value);
    if (body == nullptr) {
        return std::nullopt;
    }

    return LoweredStatementNode{LoweredForEachStatement{
        .binding = *binding_id,
        .binding_span = statement.variable_span,
        .binding_type = *inferred_type,
        .snapshot_storage = snapshot_storage,
        .iterable = std::move(iterable),
        .iterable_type = *iterable_type,
        .body = std::move(body),
    }};
}

std::optional<LoweredStatementNode> Lowerer::lower_statement_node(
    const SourceSpan span,
    const ReturnStatement& statement)
{
    if (current_function_ == nullptr) {
        report(span, "malformed semantic state: return is outside a function");
        return std::nullopt;
    }
    const auto* function = function_symbol(*current_function_);
    if (function == nullptr) {
        return std::nullopt;
    }

    if (statement.value == nullptr) {
        if (function->return_type != context_.types.void_type()) {
            report(
                span,
                current_function_->is_main
                    ? "C++ code generation requires a value in a 'main' "
                      "return statement"
                    : "malformed semantic state: non-void return has no "
                      "value");
            return std::nullopt;
        }
        return LoweredStatementNode{LoweredReturnStatement{.value = nullptr}};
    }

    if (function->return_type == context_.types.void_type()) {
        report(
            statement.value->span,
            "malformed semantic state: void return has a value");
        return std::nullopt;
    }
    const auto value_type = expression_type(*statement.value);
    if (!value_type.has_value()) {
        return std::nullopt;
    }
    if (*value_type != function->return_type) {
        report(
            statement.value->span,
            "malformed semantic state: return value type does not match its "
            "function");
        return std::nullopt;
    }
    auto value = lower_expression(*statement.value);
    if (value == nullptr) {
        return std::nullopt;
    }
    return LoweredStatementNode{LoweredReturnStatement{
        .value = std::move(value),
    }};
}

std::optional<LoweredStatementNode> Lowerer::lower_statement_node(
    const SourceSpan span,
    const BreakStatement&)
{
    if (loop_depth_ == 0) {
        report(
            span,
            "malformed semantic state: break statement is outside a loop");
        return std::nullopt;
    }
    return LoweredStatementNode{LoweredBreakStatement{}};
}

std::optional<LoweredStatementNode> Lowerer::lower_statement_node(
    const SourceSpan span,
    const ContinueStatement&)
{
    if (loop_depth_ == 0) {
        report(
            span,
            "malformed semantic state: continue statement is outside a loop");
        return std::nullopt;
    }
    return LoweredStatementNode{LoweredContinueStatement{}};
}

std::optional<LoweredStatementNode> Lowerer::lower_statement_node(
    const SourceSpan span,
    const BlockStatement& statement)
{
    if (statement.block == nullptr) {
        report(span, "malformed AST: nested block is missing its body");
        return std::nullopt;
    }
    auto block = lower_scoped_block(*statement.block);
    if (block == nullptr) {
        return std::nullopt;
    }
    return LoweredStatementNode{LoweredBlockStatement{
        .block = std::move(block),
    }};
}

std::optional<TypeId> Lowerer::expression_type(
    const Expression& expression)
{
    const auto type = context_.type_info.type_of(expression);
    if (!type.has_value()) {
        report(
            expression.span,
            "malformed semantic state: expression has no type");
        return std::nullopt;
    }
    if (!known_type(*type, expression.span, "expression")) {
        return std::nullopt;
    }
    return type;
}

LoweredExpressionPtr Lowerer::lower_expression(
    const Expression& expression,
    const bool allow_minimum_magnitude)
{
    const auto type = expression_type(expression);
    if (!type.has_value()) {
        return nullptr;
    }
    auto node = std::visit(
        [this,
         span = expression.span,
         type = *type,
         allow_minimum_magnitude](const auto& value) {
            using Node = std::decay_t<decltype(value)>;
            if constexpr (
                std::is_same_v<Node, IntegerLiteralExpression>
                || std::is_same_v<Node, ParenthesizedExpression>) {
                return lower_expression_node(
                    span,
                    type,
                    value,
                    allow_minimum_magnitude);
            } else {
                return lower_expression_node(span, type, value);
            }
        },
        expression.node);
    if (!node.has_value()) {
        return nullptr;
    }
    return std::make_unique<LoweredExpression>(LoweredExpression{
        .span = expression.span,
        .type = *type,
        .node = std::move(*node),
    });
}

std::optional<LoweredExpressionNode> Lowerer::lower_expression_node(
    const SourceSpan span,
    const TypeId result_type,
    const IntegerLiteralExpression& expression,
    const bool allow_minimum_magnitude)
{
    if (result_type != context_.types.integer_type()) {
        report(
            span,
            "malformed semantic state: integer literal does not have type "
            "'int'");
        return std::nullopt;
    }
    const auto normalized = normalize_integer_lexeme(expression.lexeme);
    if (!normalized.has_value()) {
        report(span, "malformed AST: invalid integer literal lexeme");
        return std::nullopt;
    }
    const auto maximum = allow_minimum_magnitude
        ? minimum_integer_magnitude
        : maximum_integer_magnitude;
    if (exceeds_magnitude(*normalized, maximum)) {
        report(
            span,
            "integer literal is outside the supported signed 64-bit "
            "code-generation range");
        return std::nullopt;
    }
    return LoweredExpressionNode{LoweredIntegerLiteralExpression{
        .lexeme = *normalized,
    }};
}

std::optional<LoweredExpressionNode> Lowerer::lower_expression_node(
    const SourceSpan span,
    const TypeId result_type,
    const BooleanLiteralExpression& expression)
{
    if (result_type != context_.types.boolean_type()) {
        report(
            span,
            "malformed semantic state: boolean literal does not have type "
            "'bool'");
        return std::nullopt;
    }
    return LoweredExpressionNode{LoweredBooleanLiteralExpression{
        .value = expression.value,
    }};
}

std::optional<LoweredExpressionNode> Lowerer::lower_expression_node(
    const SourceSpan span,
    const TypeId result_type,
    const CharacterLiteralExpression& expression)
{
    if (result_type != context_.types.character_type()) {
        report(
            span,
            "malformed semantic state: character literal does not have type "
            "'char'");
        return std::nullopt;
    }
    return LoweredExpressionNode{LoweredCharacterLiteralExpression{
        .value = expression.value,
    }};
}

std::optional<LoweredExpressionNode> Lowerer::lower_expression_node(
    const SourceSpan span,
    const TypeId result_type,
    const StringLiteralExpression& expression)
{
    if (result_type != context_.types.string_type()) {
        report(
            span,
            "malformed semantic state: string literal does not have type "
            "'string'");
        return std::nullopt;
    }
    return LoweredExpressionNode{LoweredStringLiteralExpression{
        .value = expression.value,
    }};
}

std::optional<LoweredExpressionNode> Lowerer::lower_expression_node(
    const SourceSpan span,
    const TypeId result_type,
    const IdentifierExpression& expression)
{
    const auto resolution = context_.resolutions.resolution_for(expression);
    if (!resolution.has_value()) {
        report(span, "malformed semantic state: identifier has no resolution");
        return std::nullopt;
    }
    const auto* id = std::get_if<SymbolId>(&*resolution);
    if (id == nullptr) {
        report(
            span,
            "malformed semantic state: value identifier resolves to a "
            "builtin");
        return std::nullopt;
    }
    const auto storage = active_storage(*id, span, "identifier reference");
    if (!storage.has_value()) {
        return std::nullopt;
    }
    if (storage->type != result_type) {
        report(
            span,
            "malformed semantic state: identifier type does not match its "
            "symbol");
        return std::nullopt;
    }
    return LoweredExpressionNode{LoweredStorageExpression{
        .storage = *id,
    }};
}

std::optional<LoweredExpressionNode> Lowerer::lower_expression_node(
    const SourceSpan span,
    const TypeId result_type,
    const UnaryExpression& expression)
{
    if (expression.operand == nullptr) {
        report(span, "malformed AST: unary expression has no operand");
        return std::nullopt;
    }
    auto operator_kind = LoweredUnaryOperator::plus;
    switch (expression.operator_kind) {
    case UnaryOperator::plus:
        operator_kind = LoweredUnaryOperator::plus;
        break;
    case UnaryOperator::minus:
        operator_kind = LoweredUnaryOperator::minus;
        break;
    case UnaryOperator::logical_not:
        operator_kind = LoweredUnaryOperator::logical_not;
        break;
    default:
        report(span, "malformed AST: unknown unary operator");
        return std::nullopt;
    }
    auto allow_minimum = false;
    if (expression.operator_kind == UnaryOperator::minus) {
        if (const auto* literal =
                unwrap_integer_literal(*expression.operand)) {
            const auto magnitude = normalize_integer_lexeme(literal->lexeme);
            allow_minimum = magnitude.has_value()
                && *magnitude == minimum_integer_magnitude;
        }
    }
    auto operand = lower_expression(*expression.operand, allow_minimum);
    if (operand == nullptr) {
        return std::nullopt;
    }
    if (expression.operator_kind == UnaryOperator::minus
        && is_minimum_integer_value(*expression.operand)) {
        report(
            span,
            "integer expression is outside the signed 64-bit range");
        return std::nullopt;
    }
    const auto expected_type =
        operator_kind == LoweredUnaryOperator::logical_not
        ? context_.types.boolean_type()
        : context_.types.integer_type();
    if (operand->type != expected_type || result_type != expected_type) {
        report(
            span,
            "malformed semantic state: unary expression types do not match "
            "its operator");
        return std::nullopt;
    }
    return LoweredExpressionNode{LoweredUnaryExpression{
        .operator_kind = operator_kind,
        .operand = std::move(operand),
    }};
}

std::optional<LoweredExpressionNode> Lowerer::lower_expression_node(
    const SourceSpan span,
    const TypeId result_type,
    const BinaryExpression& expression)
{
    if (expression.left == nullptr || expression.right == nullptr) {
        report(span, "malformed AST: binary expression is missing an operand");
        return std::nullopt;
    }
    auto operator_kind = LoweredBinaryOperator::logical_or;
    switch (expression.operator_kind) {
    case BinaryOperator::logical_or:
        operator_kind = LoweredBinaryOperator::logical_or;
        break;
    case BinaryOperator::logical_and:
        operator_kind = LoweredBinaryOperator::logical_and;
        break;
    case BinaryOperator::equal:
        operator_kind = LoweredBinaryOperator::equal;
        break;
    case BinaryOperator::not_equal:
        operator_kind = LoweredBinaryOperator::not_equal;
        break;
    case BinaryOperator::less:
        operator_kind = LoweredBinaryOperator::less;
        break;
    case BinaryOperator::less_equal:
        operator_kind = LoweredBinaryOperator::less_equal;
        break;
    case BinaryOperator::greater:
        operator_kind = LoweredBinaryOperator::greater;
        break;
    case BinaryOperator::greater_equal:
        operator_kind = LoweredBinaryOperator::greater_equal;
        break;
    case BinaryOperator::add:
        operator_kind = LoweredBinaryOperator::add;
        break;
    case BinaryOperator::subtract:
        operator_kind = LoweredBinaryOperator::subtract;
        break;
    case BinaryOperator::multiply:
        operator_kind = LoweredBinaryOperator::multiply;
        break;
    case BinaryOperator::divide:
        operator_kind = LoweredBinaryOperator::divide;
        break;
    case BinaryOperator::remainder:
        operator_kind = LoweredBinaryOperator::remainder;
        break;
    default:
        report(span, "malformed AST: unknown binary operator");
        return std::nullopt;
    }

    auto left = lower_expression(*expression.left);
    if (left == nullptr) {
        return std::nullopt;
    }
    auto right = lower_expression(*expression.right);
    if (right == nullptr) {
        return std::nullopt;
    }

    const auto matching_integers =
        left->type == context_.types.integer_type()
        && right->type == context_.types.integer_type();
    const auto matching_strings =
        left->type == context_.types.string_type()
        && right->type == context_.types.string_type();
    auto types_match = false;
    switch (operator_kind) {
    case LoweredBinaryOperator::logical_or:
    case LoweredBinaryOperator::logical_and:
        types_match =
            left->type == context_.types.boolean_type()
            && right->type == context_.types.boolean_type()
            && result_type == context_.types.boolean_type();
        break;
    case LoweredBinaryOperator::equal:
    case LoweredBinaryOperator::not_equal: {
        const auto kind = primitive_kind(left->type);
        types_match = left->type == right->type && kind.has_value()
            && *kind != PrimitiveTypeKind::void_type
            && result_type == context_.types.boolean_type();
        break;
    }
    case LoweredBinaryOperator::less:
    case LoweredBinaryOperator::less_equal:
    case LoweredBinaryOperator::greater:
    case LoweredBinaryOperator::greater_equal:
        types_match = (matching_integers || matching_strings)
            && result_type == context_.types.boolean_type();
        break;
    case LoweredBinaryOperator::add:
        types_match = (matching_integers
                && result_type == context_.types.integer_type())
            || (matching_strings
                && result_type == context_.types.string_type());
        break;
    case LoweredBinaryOperator::subtract:
    case LoweredBinaryOperator::multiply:
    case LoweredBinaryOperator::divide:
    case LoweredBinaryOperator::remainder:
        types_match = matching_integers
            && result_type == context_.types.integer_type();
        break;
    }
    if (!types_match) {
        report(
            span,
            "malformed semantic state: binary expression types do not match "
            "its operator");
        return std::nullopt;
    }
    return LoweredExpressionNode{LoweredBinaryExpression{
        .operator_kind = operator_kind,
        .left = std::move(left),
        .right = std::move(right),
    }};
}

const IdentifierExpression* Lowerer::unwrap_callable_identifier(
    const Expression& expression) const
{
    if (const auto* identifier =
            std::get_if<IdentifierExpression>(&expression.node)) {
        return identifier;
    }
    const auto* parenthesized =
        std::get_if<ParenthesizedExpression>(&expression.node);
    if (parenthesized == nullptr || parenthesized->expression == nullptr) {
        return nullptr;
    }
    return unwrap_callable_identifier(*parenthesized->expression);
}

const MemberAccessExpression* Lowerer::unwrap_member_access(
    const Expression& expression) const
{
    if (const auto* member =
            std::get_if<MemberAccessExpression>(&expression.node)) {
        return member;
    }
    const auto* parenthesized =
        std::get_if<ParenthesizedExpression>(&expression.node);
    if (parenthesized == nullptr || parenthesized->expression == nullptr) {
        return nullptr;
    }
    return unwrap_member_access(*parenthesized->expression);
}

const CallExpression* Lowerer::unwrap_call(
    const Expression& expression) const
{
    if (const auto* call = std::get_if<CallExpression>(&expression.node)) {
        return call;
    }
    const auto* parenthesized =
        std::get_if<ParenthesizedExpression>(&expression.node);
    if (parenthesized == nullptr || parenthesized->expression == nullptr) {
        return nullptr;
    }
    return unwrap_call(*parenthesized->expression);
}

std::optional<LoweredExpressionNode> Lowerer::lower_expression_node(
    const SourceSpan span,
    const TypeId result_type,
    const CallExpression& expression)
{
    if (expression.callee == nullptr) {
        report(span, "malformed AST: call has no callee");
        return std::nullopt;
    }
    if (const auto* member = unwrap_member_access(*expression.callee)) {
        return lower_member_call(
            span,
            result_type,
            expression,
            *member,
            expression.callee->span);
    }

    const auto* identifier =
        unwrap_callable_identifier(*expression.callee);
    if (identifier == nullptr) {
        report(
            expression.callee->span,
            "C++ code generation only supports direct function calls yet");
        return std::nullopt;
    }
    const auto resolution = context_.resolutions.resolution_for(*identifier);
    if (!resolution.has_value()) {
        report(
            expression.callee->span,
            "malformed semantic state: call callee has no resolution");
        return std::nullopt;
    }
    if (const auto* builtin =
            std::get_if<BuiltinFunctionKind>(&*resolution)) {
        return lower_builtin_call(
            span,
            result_type,
            expression,
            *identifier,
            *builtin);
    }
    return lower_user_call(
        span,
        result_type,
        expression,
        *identifier,
        std::get<SymbolId>(*resolution));
}

std::optional<std::vector<LoweredExpressionPtr>> Lowerer::lower_arguments(
    const SourceSpan call_span,
    const std::vector<ExpressionPtr>& arguments)
{
    std::vector<LoweredExpressionPtr> lowered;
    lowered.reserve(arguments.size());
    for (const auto& argument : arguments) {
        if (argument == nullptr) {
            report(call_span, "malformed AST: call argument is missing");
            return std::nullopt;
        }
        auto value = lower_expression(*argument);
        if (value == nullptr) {
            return std::nullopt;
        }
        lowered.push_back(std::move(value));
    }
    return lowered;
}

std::optional<LoweredExpressionNode> Lowerer::lower_builtin_call(
    const SourceSpan span,
    const TypeId result_type,
    const CallExpression& call,
    const IdentifierExpression&,
    const BuiltinFunctionKind builtin)
{
    switch (builtin) {
    case BuiltinFunctionKind::print:
        if (expression_statement_call_ != &call) {
            report(
                span,
                "C++ code generation only supports builtin 'print' as an "
                "expression statement");
            return std::nullopt;
        }
        break;
    case BuiltinFunctionKind::read_int:
    case BuiltinFunctionKind::read_string:
    case BuiltinFunctionKind::read_char:
    case BuiltinFunctionKind::len:
    case BuiltinFunctionKind::substring:
        break;
    default:
        report(span, "malformed semantic state: unknown builtin function");
        return std::nullopt;
    }

    const auto signature = builtin_function_signature(builtin);
    const auto expected_result =
        semantic_primitive_type(signature.return_type);
    if (!expected_result.has_value() || result_type != *expected_result) {
        report(
            span,
            "malformed semantic state: builtin call result type does not "
            "match its signature");
        return std::nullopt;
    }
    if (call.arguments.size() != signature.parameter_types.size()) {
        report(
            span,
            "malformed semantic state: builtin '"
                + std::string{builtin_name(builtin)}
                + "' has an unexpected number of arguments");
        return std::nullopt;
    }
    for (std::size_t index = 0; index < call.arguments.size(); ++index) {
        const auto& argument = call.arguments[index];
        if (argument == nullptr) {
            report(span, "malformed AST: builtin call argument is missing");
            return std::nullopt;
        }
        const auto argument_type = expression_type(*argument);
        const auto argument_kind = argument_type.has_value()
            ? primitive_kind(*argument_type)
            : std::nullopt;
        if (!argument_kind.has_value()
            || !builtin_parameter_accepts(
                signature.parameter_types[index],
                *argument_kind)) {
            if (argument_type.has_value()) {
                report(
                    argument->span,
                    "malformed semantic state: builtin argument type does "
                    "not match its signature");
            }
            return std::nullopt;
        }
    }

    auto arguments = lower_arguments(span, call.arguments);
    if (!arguments.has_value()) {
        return std::nullopt;
    }
    return LoweredExpressionNode{LoweredBuiltinCallExpression{
        .builtin = builtin,
        .callee_span = call.callee != nullptr ? call.callee->span : span,
        .arguments = std::move(*arguments),
    }};
}

std::optional<LoweredExpressionNode> Lowerer::lower_user_call(
    const SourceSpan span,
    const TypeId result_type,
    const CallExpression& call,
    const IdentifierExpression&,
    const SymbolId function_id)
{
    const auto callee_span =
        call.callee != nullptr ? call.callee->span : span;
    const auto* entry = symbol(function_id, callee_span, "call callee");
    if (entry == nullptr) {
        return std::nullopt;
    }
    const auto* function = std::get_if<FunctionSymbol>(&entry->data);
    if (function == nullptr) {
        report(
            callee_span,
            "malformed semantic state: resolved call target is not a "
            "function");
        return std::nullopt;
    }
    if (main_symbol_.has_value() && function_id == *main_symbol_) {
        report(callee_span, "C++ code generation cannot call 'main'");
        return std::nullopt;
    }
    if (!top_level_function_ids_.contains(function_id.value)) {
        report(
            callee_span,
            "C++ code generation does not support calls to nested functions "
            "yet");
        return std::nullopt;
    }
    if (call.arguments.size() != function->parameter_types.size()) {
        report(
            span,
            "malformed semantic state: call arity does not match resolved "
            "function");
        return std::nullopt;
    }
    if (result_type != function->return_type) {
        report(
            span,
            "malformed semantic state: call result type does not match "
            "resolved function");
        return std::nullopt;
    }

    for (std::size_t index = 0; index < call.arguments.size(); ++index) {
        const auto& argument = call.arguments[index];
        if (argument == nullptr) {
            report(span, "malformed AST: call argument is missing");
            return std::nullopt;
        }
        const auto argument_type = expression_type(*argument);
        if (!argument_type.has_value()
            || *argument_type != function->parameter_types[index]) {
            if (argument_type.has_value()) {
                report(
                    argument->span,
                    "malformed semantic state: call argument type does not "
                    "match resolved function");
            }
            return std::nullopt;
        }
    }

    auto arguments = lower_arguments(span, call.arguments);
    if (!arguments.has_value()) {
        return std::nullopt;
    }
    return LoweredExpressionNode{LoweredUserCallExpression{
        .function = function_id,
        .callee_span = callee_span,
        .arguments = std::move(*arguments),
    }};
}

std::optional<LoweredExpressionNode> Lowerer::lower_member_call(
    const SourceSpan span,
    const TypeId result_type,
    const CallExpression& call,
    const MemberAccessExpression& member,
    const SourceSpan member_span)
{
    const auto kind = context_.type_info.member_for(member);
    if (!kind.has_value()) {
        report(
            member_span,
            "malformed semantic state: string member has no semantic "
            "identity");
        return std::nullopt;
    }
    if (member.base == nullptr) {
        report(span, "malformed AST: string member has no receiver");
        return std::nullopt;
    }
    const auto receiver_type = expression_type(*member.base);
    if (!receiver_type.has_value()
        || *receiver_type != context_.types.string_type()) {
        if (receiver_type.has_value()) {
            report(
                member.base->span,
                "malformed semantic state: string member receiver does not "
                "have type 'string'");
        }
        return std::nullopt;
    }

    auto expected_arity = std::size_t{0};
    auto expected_result = context_.types.integer_type();
    switch (*kind) {
    case MemberKind::string_length:
        break;
    case MemberKind::string_push:
        expected_arity = 1;
        expected_result = context_.types.void_type();
        break;
    default:
        report(
            member_span,
            "malformed semantic state: unknown string member identity");
        return std::nullopt;
    }
    if (result_type != expected_result) {
        report(
            span,
            "malformed semantic state: string member call result type does "
            "not match its member");
        return std::nullopt;
    }
    if (call.arguments.size() != expected_arity) {
        report(
            span,
            "malformed semantic state: string member call has an unexpected "
            "number of arguments");
        return std::nullopt;
    }
    if (*kind == MemberKind::string_push) {
        if (call.arguments.front() == nullptr) {
            report(span, "malformed AST: string push argument is missing");
            return std::nullopt;
        }
        const auto argument_type =
            expression_type(*call.arguments.front());
        if (!argument_type.has_value()
            || *argument_type != context_.types.character_type()) {
            if (argument_type.has_value()) {
                report(
                    call.arguments.front()->span,
                    "malformed semantic state: string push argument does not "
                    "have type 'char'");
            }
            return std::nullopt;
        }
    }

    auto receiver = lower_expression(*member.base);
    if (receiver == nullptr) {
        return std::nullopt;
    }
    auto arguments = lower_arguments(span, call.arguments);
    if (!arguments.has_value()) {
        return std::nullopt;
    }
    return LoweredExpressionNode{LoweredMemberCallExpression{
        .member = *kind,
        .member_span = member_span,
        .receiver = std::move(receiver),
        .arguments = std::move(*arguments),
    }};
}

std::optional<LoweredExpressionNode> Lowerer::lower_expression_node(
    const SourceSpan span,
    const TypeId result_type,
    const IndexExpression& expression)
{
    if (expression.base == nullptr || expression.index == nullptr) {
        report(span, "malformed AST: index expression is missing an operand");
        return std::nullopt;
    }
    const auto base_type = expression_type(*expression.base);
    const auto index_type = expression_type(*expression.index);
    if (!base_type.has_value() || !index_type.has_value()) {
        return std::nullopt;
    }
    if (*index_type != context_.types.integer_type()) {
        report(
            expression.index->span,
            "malformed semantic state: index does not have type 'int'");
        return std::nullopt;
    }

    auto expected_result = std::optional<TypeId>{};
    if (*base_type == context_.types.string_type()) {
        expected_result = context_.types.character_type();
    } else {
        const auto descriptor = context_.types.lookup(*base_type);
        const auto* vector = descriptor.has_value()
            ? std::get_if<SemanticVectorType>(&*descriptor)
            : nullptr;
        if (vector == nullptr) {
            report(
                expression.base->span,
                "malformed semantic state: index base is not a string or "
                "vector");
            return std::nullopt;
        }
        expected_result = vector->element_type;
    }
    if (result_type != *expected_result) {
        report(
            span,
            "malformed semantic state: index result type does not match its "
            "base type");
        return std::nullopt;
    }

    auto base = lower_expression(*expression.base);
    if (base == nullptr) {
        return std::nullopt;
    }
    auto index = lower_expression(*expression.index);
    if (index == nullptr) {
        return std::nullopt;
    }
    return LoweredExpressionNode{LoweredIndexExpression{
        .container_type = *base_type,
        .base = std::move(base),
        .index = std::move(index),
    }};
}

std::optional<LoweredExpressionNode> Lowerer::lower_expression_node(
    const SourceSpan span,
    TypeId,
    const MemberAccessExpression&)
{
    report(span, "C++ code generation does not support member access yet");
    return std::nullopt;
}

std::optional<LoweredExpressionNode> Lowerer::lower_expression_node(
    const SourceSpan span,
    const TypeId result_type,
    const VectorConstructionExpression& expression)
{
    const auto descriptor = context_.types.lookup(result_type);
    const auto* vector = descriptor.has_value()
        ? std::get_if<SemanticVectorType>(&*descriptor)
        : nullptr;
    if (vector == nullptr) {
        report(
            span,
            "malformed semantic state: vector construction result has a "
            "non-vector type");
        return std::nullopt;
    }
    if (!syntax_type_matches(expression.type, result_type)) {
        report(
            expression.type.span,
            "malformed semantic state: vector construction semantic type "
            "does not match its syntax type");
        return std::nullopt;
    }
    if (expression.arguments.size() > 2) {
        report(
            span,
            "malformed semantic state: vector construction has an unexpected "
            "number of arguments");
        return std::nullopt;
    }

    for (std::size_t index = 0; index < expression.arguments.size(); ++index) {
        const auto& argument = expression.arguments[index];
        if (argument == nullptr) {
            report(
                span,
                "malformed AST: vector construction argument is missing");
            return std::nullopt;
        }
        const auto argument_type = expression_type(*argument);
        const auto expected_type = index == 0
            ? context_.types.integer_type()
            : vector->element_type;
        if (!argument_type.has_value() || *argument_type != expected_type) {
            if (argument_type.has_value()) {
                report(
                    argument->span,
                    "malformed semantic state: vector construction argument "
                    "type does not match its position");
            }
            return std::nullopt;
        }
    }

    auto arguments = std::vector<LoweredExpressionPtr>{};
    arguments.reserve(expression.arguments.size());
    for (const auto& argument : expression.arguments) {
        auto lowered = lower_expression(*argument);
        if (lowered == nullptr) {
            return std::nullopt;
        }
        arguments.push_back(std::move(lowered));
    }
    return LoweredExpressionNode{LoweredVectorConstructionExpression{
        .element_type = vector->element_type,
        .arguments = std::move(arguments),
    }};
}

std::optional<LoweredExpressionNode> Lowerer::lower_expression_node(
    const SourceSpan span,
    const TypeId result_type,
    const ParenthesizedExpression& expression,
    const bool allow_minimum_magnitude)
{
    if (expression.expression == nullptr) {
        report(
            span,
            "malformed AST: parenthesized expression has no expression");
        return std::nullopt;
    }
    auto value = lower_expression(
        *expression.expression,
        allow_minimum_magnitude);
    if (value == nullptr) {
        return std::nullopt;
    }
    if (value->type != result_type) {
        report(
            span,
            "malformed semantic state: grouped expression type does not "
            "match its operand");
        return std::nullopt;
    }
    return LoweredExpressionNode{LoweredGroupedExpression{
        .expression = std::move(value),
    }};
}

}

std::optional<LoweredProgram> lower_program(
    const Program& program,
    const LoweringContext& context,
    DiagnosticEngine& diagnostics)
{
    if (diagnostics.has_errors()) {
        return std::nullopt;
    }
    return Lowerer{context, diagnostics}.lower(program);
}

}

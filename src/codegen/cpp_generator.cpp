#include "pseudo/codegen/cpp_generator.hpp"

#include "pseudo/ast/program.hpp"
#include "pseudo/diagnostics/diagnostic_engine.hpp"
#include "pseudo/semantic/builtin.hpp"
#include "pseudo/semantic/declaration_info.hpp"
#include "pseudo/semantic/resolution_info.hpp"
#include "pseudo/semantic/symbol_table.hpp"
#include "pseudo/semantic/type_context.hpp"
#include "pseudo/semantic/type_info.hpp"

#include <algorithm>
#include <cstddef>
#include <locale>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
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

[[nodiscard]] std::string escape_cpp_bytes(
    const std::string_view bytes,
    const char delimiter)
{
    std::string escaped;

    for (const char value : bytes) {
        const auto byte = static_cast<unsigned char>(value);

        switch (byte) {
        case '\\':
            escaped += "\\\\";
            break;
        default:
            if (byte == static_cast<unsigned char>(delimiter)) {
                escaped.push_back('\\');
                escaped.push_back(delimiter);
            } else if (byte >= 0x20 && byte <= 0x7E) {
                escaped.push_back(static_cast<char>(byte));
            } else {
                escaped.push_back('\\');
                escaped.push_back(
                    static_cast<char>('0' + ((byte >> 6U) & 0x07U)));
                escaped.push_back(
                    static_cast<char>('0' + ((byte >> 3U) & 0x07U)));
                escaped.push_back(static_cast<char>('0' + (byte & 0x07U)));
            }
            break;
        }
    }

    return escaped;
}

[[nodiscard]] std::optional<std::string_view> unary_operator_spelling(
    const UnaryOperator operator_kind) noexcept
{
    switch (operator_kind) {
    case UnaryOperator::plus:
        return "+";
    case UnaryOperator::minus:
        return "-";
    case UnaryOperator::logical_not:
        return "!";
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<std::string_view> binary_operator_spelling(
    const BinaryOperator operator_kind) noexcept
{
    switch (operator_kind) {
    case BinaryOperator::logical_or:
        return "||";
    case BinaryOperator::logical_and:
        return "&&";
    case BinaryOperator::equal:
        return "==";
    case BinaryOperator::not_equal:
        return "!=";
    case BinaryOperator::less:
        return "<";
    case BinaryOperator::less_equal:
        return "<=";
    case BinaryOperator::greater:
        return ">";
    case BinaryOperator::greater_equal:
        return ">=";
    case BinaryOperator::add:
        return "+";
    case BinaryOperator::subtract:
        return "-";
    case BinaryOperator::multiply:
        return "*";
    case BinaryOperator::divide:
        return "/";
    case BinaryOperator::remainder:
        return "%";
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<std::string_view> assignment_operator_spelling(
    const AssignmentOperator operator_kind) noexcept
{
    switch (operator_kind) {
    case AssignmentOperator::assign:
        return "=";
    case AssignmentOperator::add_assign:
        return "+=";
    case AssignmentOperator::subtract_assign:
        return "-=";
    case AssignmentOperator::multiply_assign:
        return "*=";
    case AssignmentOperator::divide_assign:
        return "/=";
    case AssignmentOperator::remainder_assign:
        return "%=";
    }

    return std::nullopt;
}

[[nodiscard]] const IntegerLiteralExpression* unwrap_integer_literal(
    const Expression& expression)
{
    if (const auto* integer =
            std::get_if<IntegerLiteralExpression>(&expression.node)) {
        return integer;
    }

    const auto* parenthesized =
        std::get_if<ParenthesizedExpression>(&expression.node);
    if (parenthesized == nullptr || parenthesized->expression == nullptr) {
        return nullptr;
    }

    return unwrap_integer_literal(*parenthesized->expression);
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

[[nodiscard]] std::string generated_name(
    const std::string_view prefix,
    const SymbolId symbol)
{
    std::ostringstream name;
    name.imbue(std::locale::classic());
    name << prefix << symbol.value;
    return name.str();
}

class CppGenerator {
public:
    CppGenerator(
        const CppGenerationContext& context,
        DiagnosticEngine& diagnostics)
        : context_{context}
        , diagnostics_{diagnostics}
        , initial_error_count_{diagnostics.error_count()}
    {
        output_.imbue(std::locale::classic());
    }

    [[nodiscard]] std::optional<std::string> generate(const Program& program)
    {
        validate_program(program);
        if (has_new_errors()) {
            return std::nullopt;
        }

        auto has_prototypes = false;
        for (const auto& function : functions_) {
            if (function.is_main) {
                continue;
            }
            emit_function_signature(function);
            output_ << ";\n";
            has_prototypes = true;
        }
        if (has_prototypes) {
            output_ << '\n';
        }

        for (std::size_t index = 0; index < functions_.size(); ++index) {
            if (index != 0) {
                output_ << '\n';
            }
            emit_function_definition(functions_[index]);
        }

        if (has_new_errors()) {
            return std::nullopt;
        }

        std::ostringstream generated;
        generated.imbue(std::locale::classic());
        generated
            << "#include <cstdint>\n"
               "#include <iostream>\n"
               "#include <string>\n";
        if (uses_vector_) {
            generated << "#include <vector>\n";
        }
        if (uses_runtime_) {
            generated << "#include <pseudo/runtime.hpp>\n";
        }
        generated << '\n' << output_.str();
        return generated.str();
    }

private:
    struct FunctionEntry {
        const FunctionDeclaration* declaration;
        SymbolId symbol;
        bool is_main;
    };

    struct StorageReference {
        TypeId type;
        std::string generated_name;
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

    [[nodiscard]] std::optional<StorageReference> storage_reference(
        const SymbolId id,
        const SourceSpan span,
        const std::string_view role)
    {
        const auto* entry = symbol(id, span, role);
        if (entry == nullptr) {
            return std::nullopt;
        }

        if (const auto* parameter = std::get_if<ParameterSymbol>(&entry->data)) {
            if (!current_parameter_ids_.contains(id.value)) {
                report(
                    span,
                    "malformed semantic state: parameter reference is outside "
                    "its function");
                return std::nullopt;
            }
            return StorageReference{
                .type = parameter->type,
                .generated_name = generated_name("tpp_parameter_", id),
            };
        }

        if (const auto* variable = std::get_if<VariableSymbol>(&entry->data)) {
            if (!current_variable_ids_.contains(id.value)) {
                report(
                    span,
                    "C++ code generation only supports references to local "
                    "int, string, char, or vector variables in the current "
                    "function yet");
                return std::nullopt;
            }
            if (!variable->type.has_value()) {
                report(
                    span,
                    "malformed semantic state: local variable has no type");
                return std::nullopt;
            }
            return StorageReference{
                .type = *variable->type,
                .generated_name = generated_name("tpp_variable_", id),
            };
        }

        report(
            span,
            "malformed semantic state: " + std::string{role}
                + " is not a variable or parameter");
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::string> cpp_type(
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
            return std::nullopt;
        }

        if (const auto* vector =
                std::get_if<SemanticVectorType>(&*descriptor)) {
            const auto element = cpp_type(vector->element_type, span, role);
            if (!element.has_value() || *element == "void") {
                if (element.has_value()) {
                    report(
                        span,
                        "malformed semantic state: vector element has type "
                        "'void'");
                }
                return std::nullopt;
            }
            uses_vector_ = true;
            return "std::vector<" + *element + ">";
        }

        switch (std::get<PrimitiveTypeKind>(*descriptor)) {
        case PrimitiveTypeKind::integer:
            return std::string{"std::int64_t"};
        case PrimitiveTypeKind::boolean:
            return std::string{"bool"};
        case PrimitiveTypeKind::character:
            return std::string{"char"};
        case PrimitiveTypeKind::string:
            return std::string{"std::string"};
        case PrimitiveTypeKind::void_type:
            return std::string{"void"};
        }

        report(span, "malformed semantic state: unknown primitive type");
        return std::nullopt;
    }

    [[nodiscard]] bool is_vector_type(const TypeId type) const noexcept
    {
        const auto descriptor = context_.types.lookup(type);
        return descriptor.has_value()
            && std::holds_alternative<SemanticVectorType>(*descriptor);
    }

    [[nodiscard]] bool is_supported_local_type(const TypeId type) const noexcept
    {
        return type == context_.types.integer_type()
            || type == context_.types.string_type()
            || type == context_.types.character_type()
            || is_vector_type(type);
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

    [[nodiscard]] std::optional<PrimitiveTypeKind> semantic_primitive_kind(
        const TypeId type) const noexcept
    {
        const auto descriptor = context_.types.lookup(type);
        if (!descriptor.has_value()) {
            return std::nullopt;
        }
        const auto* primitive =
            std::get_if<PrimitiveTypeKind>(&*descriptor);
        return primitive != nullptr
            ? std::optional<PrimitiveTypeKind>{*primitive}
            : std::nullopt;
    }

    [[nodiscard]] const FunctionSymbol* function_symbol(
        const FunctionEntry& function)
    {
        const auto* entry = symbol(
            function.symbol,
            function.declaration->name_span,
            "function declaration");
        if (entry == nullptr) {
            return nullptr;
        }

        const auto* data = std::get_if<FunctionSymbol>(&entry->data);
        if (data == nullptr) {
            report(
                function.declaration->name_span,
                "malformed semantic state: function declaration symbol is "
                "not a function");
        }
        return data;
    }

    void validate_program(const Program& program)
    {
        const FunctionDeclaration* main_declaration = nullptr;

        for (const auto& declaration : program.declarations) {
            std::visit(
                [&](const auto& node) {
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
                        validate_function(node, is_main);
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

    void validate_function(
        const FunctionDeclaration& declaration,
        const bool is_main)
    {
        const auto function_id = context_.declarations.symbol_for(declaration);
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

        top_level_function_ids_.insert(function_id->value);
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
        } else {
            (void)cpp_type(
                function->return_type,
                declaration.return_type.span,
                "function return type");
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

        const auto spelling = cpp_type(
            signature_type,
            parameter.type.span,
            "parameter");
        if (spelling.has_value() && *spelling == "void") {
            report(
                parameter.type.span,
                "malformed semantic state: parameter has type 'void'");
        }
    }

    void emit_function_signature(const FunctionEntry& entry)
    {
        const auto* function = function_symbol(entry);
        if (function == nullptr) {
            return;
        }

        if (entry.is_main) {
            output_ << "int main()";
            return;
        }

        const auto return_type = cpp_type(
            function->return_type,
            entry.declaration->return_type.span,
            "function return type");
        if (!return_type.has_value()) {
            return;
        }
        output_ << *return_type << ' '
                << generated_name("tpp_function_", entry.symbol) << '(';

        for (std::size_t index = 0;
             index < entry.declaration->parameters.size();
             ++index) {
            if (index != 0) {
                output_ << ", ";
            }
            const auto& parameter = entry.declaration->parameters[index];
            const auto parameter_id =
                context_.declarations.symbol_for(parameter);
            if (!parameter_id.has_value()
                || index >= function->parameter_types.size()) {
                report(
                    parameter.name_span,
                    "malformed semantic state: parameter is missing from "
                    "function signature");
                continue;
            }
            const auto parameter_type = cpp_type(
                function->parameter_types[index],
                parameter.type.span,
                "parameter");
            if (!parameter_type.has_value()) {
                continue;
            }
            output_ << *parameter_type << ' '
                    << generated_name("tpp_parameter_", *parameter_id);
        }
        output_ << ')';
    }

    void emit_function_definition(const FunctionEntry& entry)
    {
        emit_function_signature(entry);
        output_ << "\n{\n";

        current_function_ = &entry;
        current_parameter_ids_.clear();
        current_variable_ids_.clear();
        for (const auto& parameter : entry.declaration->parameters) {
            if (const auto id = context_.declarations.symbol_for(parameter)) {
                current_parameter_ids_.insert(id->value);
            }
        }

        if (entry.declaration->body != nullptr) {
            emit_body(*entry.declaration->body);
        }
        output_ << "}\n";
        current_parameter_ids_.clear();
        current_variable_ids_.clear();
        current_function_ = nullptr;
    }

    void emit_body(const Block& body)
    {
        for (const auto& item : body.items) {
            std::visit(
                [this](const auto& node) {
                    using Node = std::decay_t<decltype(node)>;
                    if constexpr (std::is_same_v<Node, FunctionDeclaration>) {
                        report(
                            node.span,
                            "C++ code generation does not support nested "
                            "functions yet");
                    } else {
                        emit_statement(node);
                    }
                },
                item);
        }
    }

    void emit_statement(const Statement& statement)
    {
        std::visit(
            [this, &statement](const auto& node) {
                emit_statement_node(statement.span, node);
            },
            statement.node);
    }

    void emit_statement_node(
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
            return;
        }

        const auto* entry = symbol(
            *declaration_id,
            declaration.name_span,
            "local variable declaration");
        if (entry == nullptr) {
            return;
        }
        const auto* variable = std::get_if<VariableSymbol>(&entry->data);
        if (variable == nullptr) {
            report(
                declaration.name_span,
                "malformed semantic state: local variable declaration symbol "
                "is not a variable");
            return;
        }
        if (!variable->type.has_value()) {
            report(
                declaration.name_span,
                "malformed semantic state: local variable has no declared "
                "type");
            return;
        }
        if (!syntax_type_matches(declaration.type, *variable->type)) {
            report(
                declaration.type.span,
                "malformed semantic state: local variable semantic type does "
                "not match its declaration");
            return;
        }
        if (!is_supported_local_type(*variable->type)) {
            report(
                declaration.type.span,
                "C++ code generation only supports local variables of type "
                "'int', 'string', 'char', or 'vector<T>' yet");
            return;
        }
        if (declaration.initializer == nullptr) {
            report(
                span,
                "C++ code generation only supports initialized local int, "
                "string, char, or vector variables yet");
            return;
        }

        const auto initializer_type =
            context_.type_info.type_of(*declaration.initializer);
        if (!initializer_type.has_value()) {
            report(
                declaration.initializer->span,
                "malformed semantic state: local variable initializer has no "
                "type");
            return;
        }
        if (*initializer_type != *variable->type) {
            report(
                declaration.initializer->span,
                "malformed semantic state: local variable initializer type "
                "does not match its declaration");
            return;
        }

        const auto variable_type = cpp_type(
            *variable->type,
            declaration.type.span,
            "local variable");
        if (!variable_type.has_value()) {
            return;
        }

        output_ << "    " << *variable_type << ' '
                << generated_name("tpp_variable_", *declaration_id) << " = ";
        if (!emit_expression(*declaration.initializer)) {
            output_ << ";\n";
            return;
        }
        output_ << ";\n";
        current_variable_ids_.insert(declaration_id->value);
    }

    [[nodiscard]] std::optional<std::vector<TypeId>>
    validate_assignment_indices(
        const AssignmentTarget& target,
        const TypeId storage_type,
        const TypeId recorded_target_type)
    {
        auto current_type = storage_type;
        std::vector<TypeId> container_types;
        container_types.reserve(target.indices.size());

        for (const auto& index : target.indices) {
            if (index == nullptr) {
                report(
                    target.span,
                    "malformed AST: assignment target index is missing");
                return std::nullopt;
            }
            const auto index_type = context_.type_info.type_of(*index);
            if (!index_type.has_value()
                || *index_type != context_.types.integer_type()) {
                report(
                    index->span,
                    "malformed semantic state: assignment target index does "
                    "not have type 'int'");
                return std::nullopt;
            }

            container_types.push_back(current_type);
            if (current_type == context_.types.string_type()) {
                current_type = context_.types.character_type();
                continue;
            }

            const auto descriptor = context_.types.lookup(current_type);
            const auto* vector = descriptor.has_value()
                ? std::get_if<SemanticVectorType>(&*descriptor)
                : nullptr;
            if (vector == nullptr) {
                report(
                    target.span,
                    "malformed semantic state: assignment target indexes a "
                    "non-indexable type");
                return std::nullopt;
            }
            current_type = vector->element_type;
        }

        if (current_type != recorded_target_type) {
            report(
                target.span,
                "malformed semantic state: assignment target type does not "
                "match its indexed symbol type");
            return std::nullopt;
        }
        return container_types;
    }

    [[nodiscard]] bool emit_assignment_target(
        const AssignmentTarget& target,
        const StorageReference& storage,
        const std::vector<TypeId>& container_types,
        const std::size_t depth)
    {
        if (depth == 0) {
            output_ << storage.generated_name;
            return true;
        }

        const auto container_type = container_types[depth - 1];
        if (container_type == context_.types.string_type()) {
            output_ << "tpp::runtime::string_index(";
        } else {
            const auto descriptor = context_.types.lookup(container_type);
            if (!descriptor.has_value()
                || !std::holds_alternative<SemanticVectorType>(*descriptor)) {
                report(
                    target.span,
                    "malformed semantic state: assignment target has an "
                    "invalid indexed container type");
                return false;
            }
            uses_vector_ = true;
            output_ << "tpp::runtime::vector_index(";
        }
        uses_runtime_ = true;

        if (!emit_assignment_target(target, storage, container_types, depth - 1)) {
            output_ << ')';
            return false;
        }
        output_ << ", ";
        const auto& index = target.indices[depth - 1];
        if (index == nullptr || !emit_expression(*index)) {
            output_ << ')';
            return false;
        }
        output_ << ')';
        return true;
    }

    void emit_statement_node(
        const SourceSpan span,
        const AssignmentStatement& statement)
    {
        if (statement.value == nullptr) {
            report(span, "malformed AST: assignment has no value");
            return;
        }

        const auto resolution =
            context_.resolutions.resolution_for(statement.target);
        if (!resolution.has_value()) {
            report(
                statement.target.name_span,
                "malformed semantic state: assignment target has no "
                "resolution");
            return;
        }
        const auto* target_id = std::get_if<SymbolId>(&*resolution);
        if (target_id == nullptr) {
            report(
                statement.target.name_span,
                "malformed semantic state: assignment target resolves to a "
                "builtin");
            return;
        }
        const auto storage = storage_reference(
            *target_id,
            statement.target.name_span,
            "assignment target");
        if (!storage.has_value()) {
            return;
        }

        const auto target_type = context_.type_info.type_of(statement.target);
        if (!target_type.has_value()) {
            report(
                statement.target.span,
                "malformed semantic state: assignment target has no type");
            return;
        }
        const auto value_type = context_.type_info.type_of(*statement.value);
        if (!value_type.has_value()) {
            report(
                statement.value->span,
                "malformed semantic state: assignment value has no type");
            return;
        }
        if (*value_type != *target_type) {
            report(
                statement.value->span,
                "malformed semantic state: assignment value type does not "
                "match its target");
            return;
        }

        const auto container_types = validate_assignment_indices(
            statement.target,
            storage->type,
            *target_type);
        if (!container_types.has_value()) {
            return;
        }

        const auto spelling =
            assignment_operator_spelling(statement.operator_kind);
        if (!spelling.has_value()) {
            report(span, "malformed AST: unknown assignment operator");
            return;
        }
        const auto direct_assignment =
            statement.operator_kind == AssignmentOperator::assign;
        const auto integer_compound =
            *target_type == context_.types.integer_type();
        const auto string_addition =
            statement.operator_kind == AssignmentOperator::add_assign
            && *target_type == context_.types.string_type();
        const auto indexed_target = !statement.target.indices.empty();
        const auto supported_direct_assignment = !indexed_target
            && ((direct_assignment
                    && (*target_type == context_.types.string_type()
                        || *target_type == context_.types.character_type()
                        || is_vector_type(*target_type)))
                || string_addition);
        const auto supported_indexed_assignment = indexed_target
            && (direct_assignment || integer_compound || string_addition);
        if (!supported_direct_assignment
            && !supported_indexed_assignment) {
            report(
                span,
                indexed_target
                    ? "malformed semantic state: assignment operator is "
                      "incompatible with its indexed target type"
                    : "C++ code generation only supports '=' for string, "
                      "char, and vector assignments and '+=' for string "
                      "assignments yet");
            return;
        }
        if (!context_.types.lookup(*target_type).has_value()
            || *target_type == context_.types.void_type()) {
            report(
                statement.target.span,
                "malformed semantic state: assignment target has an invalid "
                "type");
            return;
        }

        output_ << "    ";
        if (!emit_assignment_target(
                statement.target,
                *storage,
                *container_types,
                statement.target.indices.size())) {
            output_ << ";\n";
            return;
        }
        output_ << ' ' << *spelling << ' ';
        if (!emit_expression(*statement.value)) {
            output_ << ";\n";
            return;
        }
        output_ << ";\n";
    }

    void emit_statement_node(
        const SourceSpan span,
        const ExpressionStatement& statement)
    {
        if (statement.expression == nullptr) {
            report(
                span,
                "malformed AST: expression statement has no expression");
            return;
        }

        const auto* call = unwrap_call(*statement.expression);
        if (call == nullptr) {
            report(
                statement.expression->span,
                "C++ code generation only supports function calls as "
                "expression statements");
            return;
        }

        const auto* member = call->callee != nullptr
            ? unwrap_member_access(*call->callee)
            : nullptr;
        if (member == nullptr) {
            const auto target =
                resolve_call_target(*call, statement.expression->span);
            if (!target.has_value()) {
                return;
            }
            if (const auto* builtin =
                    std::get_if<BuiltinFunctionKind>(&*target);
                builtin != nullptr && *builtin == BuiltinFunctionKind::print) {
                emit_print_statement(*statement.expression, *call);
                return;
            }
        }

        output_ << "    ";
        if (!emit_expression(*statement.expression)) {
            output_ << '\n';
            return;
        }
        output_ << ";\n";
    }

    void emit_statement_node(const SourceSpan span, const IfStatement&)
    {
        report(span, "C++ code generation does not support if statements yet");
    }

    void emit_statement_node(const SourceSpan span, const WhileStatement&)
    {
        report(
            span,
            "C++ code generation does not support while statements yet");
    }

    void emit_statement_node(const SourceSpan span, const ForRangeStatement&)
    {
        report(
            span,
            "C++ code generation does not support for-range statements yet");
    }

    void emit_statement_node(const SourceSpan span, const ForEachStatement&)
    {
        report(
            span,
            "C++ code generation does not support for-each statements yet");
    }

    void emit_statement_node(
        const SourceSpan span,
        const ReturnStatement& statement)
    {
        if (current_function_ == nullptr) {
            report(
                span,
                "malformed semantic state: return is outside a function");
            return;
        }
        const auto* function = function_symbol(*current_function_);
        if (function == nullptr) {
            return;
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
                return;
            }
            output_ << "    return;\n";
            return;
        }

        if (function->return_type == context_.types.void_type()) {
            report(
                statement.value->span,
                "malformed semantic state: void return has a value");
            return;
        }

        const auto value_type =
            context_.type_info.type_of(*statement.value);
        if (!value_type.has_value()) {
            report(
                statement.value->span,
                "malformed semantic state: expression has no type");
            return;
        }
        if (*value_type != function->return_type) {
            report(
                statement.value->span,
                "malformed semantic state: return value type does not match "
                "its function");
            return;
        }

        if (current_function_->is_main) {
            output_ << "    return static_cast<int>(";
            if (!emit_expression(*statement.value)) {
                output_ << ");\n";
                return;
            }
            output_ << ");\n";
            return;
        }

        output_ << "    return ";
        if (!emit_expression(*statement.value)) {
            output_ << ";\n";
            return;
        }
        output_ << ";\n";
    }

    void emit_statement_node(const SourceSpan span, const BreakStatement&)
    {
        report(span, "C++ code generation does not support break statements yet");
    }

    void emit_statement_node(const SourceSpan span, const ContinueStatement&)
    {
        report(
            span,
            "C++ code generation does not support continue statements yet");
    }

    void emit_statement_node(const SourceSpan span, const BlockStatement&)
    {
        report(span, "C++ code generation does not support nested blocks yet");
    }

    [[nodiscard]] const CallExpression* unwrap_call(
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

    [[nodiscard]] const IdentifierExpression* unwrap_callable_identifier(
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

    [[nodiscard]] std::optional<ResolutionTarget> resolve_call_target(
        const CallExpression& call,
        const SourceSpan call_span)
    {
        if (call.callee == nullptr) {
            report(call_span, "malformed AST: call has no callee");
            return std::nullopt;
        }
        const auto* identifier = unwrap_callable_identifier(*call.callee);
        if (identifier == nullptr) {
            if (unwrap_member_access(*call.callee) != nullptr) {
                report(
                    call.callee->span,
                    "C++ code generation does not support member access yet");
                return std::nullopt;
            }
            report(
                call.callee->span,
                "C++ code generation only supports direct function calls "
                "yet");
            return std::nullopt;
        }
        const auto resolution = context_.resolutions.resolution_for(*identifier);
        if (!resolution.has_value()) {
            report(
                call.callee->span,
                "malformed semantic state: call callee has no resolution");
            return std::nullopt;
        }
        return resolution;
    }

    [[nodiscard]] const MemberAccessExpression* unwrap_member_access(
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

    void emit_print_statement(
        const Expression& statement_expression,
        const CallExpression& call)
    {
        if (!validate_expression_type(statement_expression)) {
            return;
        }
        if (call.arguments.size() != 1) {
            report(
                statement_expression.span,
                "malformed semantic state: builtin 'print' does not have "
                "exactly one argument");
            return;
        }
        if (call.arguments.front() == nullptr) {
            report(
                statement_expression.span,
                "malformed AST: 'print' argument is missing");
            return;
        }
        const auto signature =
            builtin_function_signature(BuiltinFunctionKind::print);
        const auto result_type =
            context_.type_info.type_of(statement_expression);
        const auto expected_result =
            semantic_primitive_type(signature.return_type);
        const auto argument_type =
            context_.type_info.type_of(*call.arguments.front());
        const auto argument_kind = argument_type.has_value()
            ? semantic_primitive_kind(*argument_type)
            : std::nullopt;
        if (!expected_result.has_value() || !result_type.has_value()
            || *result_type != *expected_result
            || signature.parameter_types.size() != 1
            || !argument_kind.has_value()
            || !builtin_parameter_accepts(
                signature.parameter_types.front(),
                *argument_kind)) {
            report(
                statement_expression.span,
                "malformed semantic state: builtin 'print' call does not "
                "match its signature");
            return;
        }

        output_ << "    std::cout << std::boolalpha << ";
        if (!emit_expression(*call.arguments.front())) {
            output_ << '\n';
            return;
        }
        output_ << " << '\\n';\n";
    }

    [[nodiscard]] bool validate_expression_type(const Expression& expression)
    {
        const auto type = context_.type_info.type_of(expression);
        if (!type.has_value()) {
            report(
                expression.span,
                "malformed semantic state: expression has no type");
            return false;
        }
        const auto descriptor = context_.types.lookup(*type);
        if (!descriptor.has_value()) {
            report(
                expression.span,
                "malformed semantic state: expression has an unknown type");
            return false;
        }
        return true;
    }

    [[nodiscard]] bool emit_expression(const Expression& expression)
    {
        if (!validate_expression_type(expression)) {
            return false;
        }
        const auto result_type = context_.type_info.type_of(expression);
        if (!result_type.has_value()) {
            return false;
        }
        return std::visit(
            [this, &expression, result_type](const auto& node) {
                using Node = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<Node, CallExpression>
                    || std::is_same_v<Node, IdentifierExpression>
                    || std::is_same_v<Node, IndexExpression>
                    || std::is_same_v<Node, VectorConstructionExpression>) {
                    return emit_expression_node(
                        expression.span,
                        node,
                        *result_type);
                } else {
                    return emit_expression_node(expression.span, node);
                }
            },
            expression.node);
    }

    [[nodiscard]] bool emit_expression_node(
        const SourceSpan span,
        const IntegerLiteralExpression& expression)
    {
        const auto normalized = normalize_integer_lexeme(expression.lexeme);
        if (!normalized.has_value()) {
            report(span, "malformed AST: invalid integer literal lexeme");
            return false;
        }
        if (exceeds_magnitude(*normalized, maximum_integer_magnitude)) {
            report(
                span,
                "integer literal is outside the supported signed 64-bit "
                "code-generation range");
            return false;
        }

        output_ << "std::int64_t{" << *normalized << '}';
        return true;
    }

    [[nodiscard]] bool emit_expression_node(
        SourceSpan,
        const BooleanLiteralExpression& expression)
    {
        output_ << (expression.value ? "true" : "false");
        return true;
    }

    [[nodiscard]] bool emit_expression_node(
        SourceSpan,
        const CharacterLiteralExpression& expression)
    {
        output_ << '\''
                << escape_cpp_bytes(
                       std::string_view{&expression.value, 1},
                       '\'')
                << '\'';
        return true;
    }

    [[nodiscard]] bool emit_expression_node(
        SourceSpan,
        const StringLiteralExpression& expression)
    {
        output_ << "std::string{\""
                << escape_cpp_bytes(expression.value, '"')
                << "\", " << expression.value.size() << '}';
        return true;
    }

    [[nodiscard]] bool emit_expression_node(
        const SourceSpan span,
        const IdentifierExpression& expression,
        const TypeId result_type)
    {
        const auto resolution =
            context_.resolutions.resolution_for(expression);
        if (!resolution.has_value()) {
            report(
                span,
                "malformed semantic state: identifier has no resolution");
            return false;
        }
        const auto* id = std::get_if<SymbolId>(&*resolution);
        if (id == nullptr) {
            report(
                span,
                "malformed semantic state: value identifier resolves to a "
                "builtin");
            return false;
        }
        const auto storage =
            storage_reference(*id, span, "identifier reference");
        if (!storage.has_value()) {
            return false;
        }
        if (storage->type != result_type) {
            report(
                span,
                "malformed semantic state: identifier type does not match "
                "its symbol");
            return false;
        }
        if (!is_supported_local_type(storage->type)
            && !current_parameter_ids_.contains(id->value)) {
            report(
                span,
                "C++ code generation only supports int, string, char, or "
                "vector local identifier expressions yet");
            return false;
        }
        output_ << storage->generated_name;
        return true;
    }

    [[nodiscard]] bool emit_expression_node(
        const SourceSpan span,
        const UnaryExpression& expression)
    {
        if (expression.operand == nullptr) {
            report(span, "malformed AST: unary expression has no operand");
            return false;
        }

        if (expression.operator_kind == UnaryOperator::minus) {
            const auto* integer = unwrap_integer_literal(*expression.operand);
            if (integer != nullptr) {
                const auto normalized =
                    normalize_integer_lexeme(integer->lexeme);
                if (normalized.has_value()
                    && *normalized == minimum_integer_magnitude) {
                    output_
                        << "(-std::int64_t{9223372036854775807} "
                           "- std::int64_t{1})";
                    return true;
                }
            }
        }

        const auto spelling = unary_operator_spelling(expression.operator_kind);
        if (!spelling.has_value()) {
            report(span, "malformed AST: unknown unary operator");
            return false;
        }

        output_ << '(' << *spelling;
        if (!emit_expression(*expression.operand)) {
            output_ << ')';
            return false;
        }
        output_ << ')';
        return true;
    }

    [[nodiscard]] bool emit_expression_node(
        const SourceSpan span,
        const BinaryExpression& expression)
    {
        if (expression.left == nullptr || expression.right == nullptr) {
            report(
                span,
                "malformed AST: binary expression is missing an operand");
            return false;
        }
        const auto spelling = binary_operator_spelling(expression.operator_kind);
        if (!spelling.has_value()) {
            report(span, "malformed AST: unknown binary operator");
            return false;
        }

        output_ << '(';
        if (!emit_expression(*expression.left)) {
            output_ << ')';
            return false;
        }
        output_ << ' ' << *spelling << ' ';
        if (!emit_expression(*expression.right)) {
            output_ << ')';
            return false;
        }
        output_ << ')';
        return true;
    }

    [[nodiscard]] bool emit_expression_node(
        const SourceSpan span,
        const CallExpression& expression,
        const TypeId result_type)
    {
        if (expression.callee != nullptr) {
            if (const auto* member =
                    unwrap_member_access(*expression.callee)) {
                return emit_member_call(
                    span,
                    expression,
                    *member,
                    result_type);
            }
        }

        const auto target = resolve_call_target(expression, span);
        if (!target.has_value()) {
            return false;
        }

        if (const auto* builtin =
                std::get_if<BuiltinFunctionKind>(&*target)) {
            return emit_builtin_call(span, expression, *builtin, result_type);
        }

        return emit_user_call(
            span,
            expression,
            std::get<SymbolId>(*target),
            result_type);
    }

    [[nodiscard]] bool emit_builtin_call(
        const SourceSpan span,
        const CallExpression& call,
        const BuiltinFunctionKind builtin,
        const TypeId result_type)
    {
        switch (builtin) {
        case BuiltinFunctionKind::print:
            report(
                span,
                "C++ code generation only supports builtin 'print' as an "
                "expression statement");
            return false;
        case BuiltinFunctionKind::read_int:
        case BuiltinFunctionKind::read_string:
        case BuiltinFunctionKind::read_char:
        case BuiltinFunctionKind::len:
        case BuiltinFunctionKind::substring:
            break;
        default:
            report(span, "malformed semantic state: unknown builtin function");
            return false;
        }

        const auto signature = builtin_function_signature(builtin);
        const auto expected_result =
            semantic_primitive_type(signature.return_type);
        if (!expected_result.has_value() || result_type != *expected_result) {
            report(
                span,
                "malformed semantic state: builtin call result type does not "
                "match its signature");
            return false;
        }
        if (call.arguments.size() != signature.parameter_types.size()) {
            report(
                span,
                "malformed semantic state: builtin '"
                    + std::string{builtin_name(builtin)}
                    + "' has an unexpected number of arguments");
            return false;
        }
        for (std::size_t index = 0; index < call.arguments.size(); ++index) {
            const auto& argument = call.arguments[index];
            if (argument == nullptr) {
                report(span, "malformed AST: builtin call argument is missing");
                return false;
            }
            const auto argument_type = context_.type_info.type_of(*argument);
            const auto argument_kind = argument_type.has_value()
                ? semantic_primitive_kind(*argument_type)
                : std::nullopt;
            if (!argument_kind.has_value()
                || !builtin_parameter_accepts(
                    signature.parameter_types[index],
                    *argument_kind)) {
                report(
                    argument->span,
                    "malformed semantic state: builtin argument type does not "
                    "match its signature");
                return false;
            }
        }

        uses_runtime_ = true;
        switch (builtin) {
        case BuiltinFunctionKind::read_int:
            output_ << "tpp::runtime::read_int()";
            return true;
        case BuiltinFunctionKind::read_string:
            output_ << "tpp::runtime::read_string()";
            return true;
        case BuiltinFunctionKind::read_char:
            output_ << "tpp::runtime::read_char()";
            return true;
        case BuiltinFunctionKind::len:
            output_ << "tpp::runtime::string_length(";
            if (!emit_expression(*call.arguments[0])) {
                output_ << ')';
                return false;
            }
            output_ << ')';
            return true;
        case BuiltinFunctionKind::substring:
            output_ << "tpp::runtime::substring(";
            for (std::size_t index = 0; index < call.arguments.size(); ++index) {
                if (index != 0) {
                    output_ << ", ";
                }
                if (!emit_expression(*call.arguments[index])) {
                    output_ << ')';
                    return false;
                }
            }
            output_ << ')';
            return true;
        case BuiltinFunctionKind::print:
            break;
        }

        report(span, "malformed semantic state: unknown builtin function");
        return false;
    }

    [[nodiscard]] bool emit_member_call(
        const SourceSpan span,
        const CallExpression& call,
        const MemberAccessExpression& member,
        const TypeId result_type)
    {
        const auto kind = context_.type_info.member_for(member);
        if (!kind.has_value()) {
            report(
                call.callee != nullptr ? call.callee->span : span,
                "malformed semantic state: string member has no semantic "
                "identity");
            return false;
        }
        if (member.base == nullptr) {
            report(span, "malformed AST: string member has no receiver");
            return false;
        }

        const auto receiver_type = context_.type_info.type_of(*member.base);
        if (!receiver_type.has_value()
            || *receiver_type != context_.types.string_type()) {
            report(
                member.base->span,
                "malformed semantic state: string member receiver does not "
                "have type 'string'");
            return false;
        }

        auto expected_arity = std::size_t{0};
        auto expected_result = context_.types.integer_type();
        auto helper = std::string_view{"tpp::runtime::string_length("};
        switch (*kind) {
        case MemberKind::string_length:
            break;
        case MemberKind::string_push:
            expected_arity = 1;
            expected_result = context_.types.void_type();
            helper = "tpp::runtime::string_push(";
            break;
        default:
            report(
                call.callee != nullptr ? call.callee->span : span,
                "malformed semantic state: unknown string member identity");
            return false;
        }
        if (result_type != expected_result) {
            report(
                span,
                "malformed semantic state: string member call result type "
                "does not match its member");
            return false;
        }
        if (call.arguments.size() != expected_arity) {
            report(
                span,
                "malformed semantic state: string member call has an "
                "unexpected number of arguments");
            return false;
        }
        if (*kind == MemberKind::string_push
            && call.arguments.front() == nullptr) {
            report(span, "malformed AST: string push argument is missing");
            return false;
        }
        if (*kind == MemberKind::string_push) {
            const auto argument_type =
                context_.type_info.type_of(*call.arguments.front());
            if (!argument_type.has_value()
                || *argument_type != context_.types.character_type()) {
                report(
                    call.arguments.front()->span,
                    "malformed semantic state: string push argument does not "
                    "have type 'char'");
                return false;
            }
        }

        uses_runtime_ = true;
        output_ << helper;
        if (!emit_expression(*member.base)) {
            output_ << ')';
            return false;
        }
        if (*kind == MemberKind::string_push) {
            output_ << ", ";
            if (!emit_expression(*call.arguments.front())) {
                output_ << ')';
                return false;
            }
        }
        output_ << ')';
        return true;
    }

    [[nodiscard]] bool emit_user_call(
        const SourceSpan span,
        const CallExpression& call,
        const SymbolId function_id,
        const TypeId result_type)
    {
        const auto callee_span =
            call.callee != nullptr ? call.callee->span : span;
        const auto* entry = symbol(function_id, callee_span, "call callee");
        if (entry == nullptr) {
            return false;
        }
        const auto* function = std::get_if<FunctionSymbol>(&entry->data);
        if (function == nullptr) {
            report(
                callee_span,
                "malformed semantic state: resolved call target is not a "
                "function");
            return false;
        }
        if (main_symbol_.has_value() && function_id == *main_symbol_) {
            report(
                callee_span,
                "C++ code generation cannot call 'main'");
            return false;
        }
        if (!top_level_function_ids_.contains(function_id.value)) {
            report(
                callee_span,
                "C++ code generation does not support calls to nested "
                "functions yet");
            return false;
        }
        if (call.arguments.size() != function->parameter_types.size()) {
            report(
                span,
                "malformed semantic state: call arity does not match "
                "resolved function");
            return false;
        }
        if (result_type != function->return_type) {
            report(
                span,
                "malformed semantic state: call result type does not match "
                "resolved function");
            return false;
        }

        for (std::size_t index = 0; index < call.arguments.size(); ++index) {
            const auto& argument = call.arguments[index];
            if (argument == nullptr) {
                report(span, "malformed AST: call argument is missing");
                return false;
            }
            const auto argument_type = context_.type_info.type_of(*argument);
            if (!argument_type.has_value()
                || *argument_type != function->parameter_types[index]) {
                report(
                    argument->span,
                    "malformed semantic state: call argument type does not "
                    "match resolved function");
                return false;
            }
        }

        output_ << generated_name("tpp_function_", function_id) << '(';
        auto valid = true;
        for (std::size_t index = 0; index < call.arguments.size(); ++index) {
            if (index != 0) {
                output_ << ", ";
            }
            if (!emit_expression(*call.arguments[index])) {
                valid = false;
            }
        }
        output_ << ')';
        return valid;
    }

    [[nodiscard]] bool emit_expression_node(
        const SourceSpan span,
        const IndexExpression& expression,
        const TypeId result_type)
    {
        if (expression.base == nullptr || expression.index == nullptr) {
            report(
                span,
                "malformed AST: index expression is missing an operand");
            return false;
        }
        const auto base_type = context_.type_info.type_of(*expression.base);
        const auto index_type = context_.type_info.type_of(*expression.index);
        if (!base_type.has_value() || !index_type.has_value()) {
            report(
                span,
                "malformed semantic state: index expression operand has no "
                "type");
            return false;
        }
        if (*index_type != context_.types.integer_type()) {
            report(
                expression.index->span,
                "malformed semantic state: index does not have type "
                "'int'");
            return false;
        }

        auto helper = std::string_view{};
        auto expected_result = std::optional<TypeId>{};
        if (*base_type == context_.types.string_type()) {
            helper = "tpp::runtime::string_index(";
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
                return false;
            }
            helper = "tpp::runtime::vector_index(";
            expected_result = vector->element_type;
            uses_vector_ = true;
        }
        if (!expected_result.has_value() || result_type != *expected_result) {
            report(
                span,
                "malformed semantic state: index result type does not match "
                "its base type");
            return false;
        }

        uses_runtime_ = true;
        output_ << helper;
        if (!emit_expression(*expression.base)) {
            output_ << ')';
            return false;
        }
        output_ << ", ";
        if (!emit_expression(*expression.index)) {
            output_ << ')';
            return false;
        }
        output_ << ')';
        return true;
    }

    [[nodiscard]] bool emit_expression_node(
        const SourceSpan span,
        const MemberAccessExpression&)
    {
        report(span, "C++ code generation does not support member access yet");
        return false;
    }

    [[nodiscard]] bool emit_expression_node(
        const SourceSpan span,
        const VectorConstructionExpression& expression,
        const TypeId result_type)
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
            return false;
        }
        if (!syntax_type_matches(expression.type, result_type)) {
            report(
                expression.type.span,
                "malformed semantic state: vector construction semantic type "
                "does not match its syntax type");
            return false;
        }
        if (expression.arguments.size() > 2) {
            report(
                span,
                "malformed semantic state: vector construction has an "
                "unexpected number of arguments");
            return false;
        }

        for (std::size_t index = 0;
             index < expression.arguments.size();
             ++index) {
            const auto& argument = expression.arguments[index];
            if (argument == nullptr) {
                report(
                    span,
                    "malformed AST: vector construction argument is missing");
                return false;
            }
            const auto argument_type = context_.type_info.type_of(*argument);
            const auto expected_type = index == 0
                ? context_.types.integer_type()
                : vector->element_type;
            if (!argument_type.has_value()
                || *argument_type != expected_type) {
                report(
                    argument->span,
                    "malformed semantic state: vector construction argument "
                    "type does not match its position");
                return false;
            }
        }

        const auto vector_type = cpp_type(
            result_type,
            expression.type.span,
            "vector construction");
        if (!vector_type.has_value()) {
            return false;
        }
        if (expression.arguments.empty()) {
            output_ << *vector_type << "{}";
            return true;
        }

        const auto element_type = cpp_type(
            vector->element_type,
            expression.type.span,
            "vector element");
        if (!element_type.has_value() || *element_type == "void") {
            return false;
        }
        uses_runtime_ = true;
        output_ << "tpp::runtime::make_vector<" << *element_type << ">(";
        auto valid = true;
        for (std::size_t index = 0;
             index < expression.arguments.size();
             ++index) {
            if (index != 0) {
                output_ << ", ";
            }
            if (!emit_expression(*expression.arguments[index])) {
                valid = false;
            }
        }
        output_ << ')';
        return valid;
    }

    [[nodiscard]] bool emit_expression_node(
        const SourceSpan span,
        const ParenthesizedExpression& expression)
    {
        if (expression.expression == nullptr) {
            report(
                span,
                "malformed AST: parenthesized expression has no expression");
            return false;
        }

        output_ << '(';
        if (!emit_expression(*expression.expression)) {
            output_ << ')';
            return false;
        }
        output_ << ')';
        return true;
    }

    const CppGenerationContext& context_;
    DiagnosticEngine& diagnostics_;
    std::size_t initial_error_count_;
    std::ostringstream output_;
    std::vector<FunctionEntry> functions_;
    std::unordered_set<std::size_t> top_level_function_ids_;
    std::optional<SymbolId> main_symbol_;
    const FunctionEntry* current_function_{nullptr};
    std::unordered_set<std::size_t> current_parameter_ids_;
    std::unordered_set<std::size_t> current_variable_ids_;
    bool uses_vector_{false};
    bool uses_runtime_{false};
};

}

std::optional<std::string> generate_cpp(
    const Program& program,
    const CppGenerationContext& context,
    DiagnosticEngine& diagnostics)
{
    if (diagnostics.has_errors()) {
        return std::nullopt;
    }

    return CppGenerator{context, diagnostics}.generate(program);
}
    
}

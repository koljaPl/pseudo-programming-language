#include "pseudo/codegen/cpp_generator.hpp"

#include "pseudo/diagnostics/diagnostic_engine.hpp"
#include "pseudo/lowering/lowered_ir.hpp"
#include "pseudo/semantic/builtin.hpp"
#include "pseudo/semantic/type_context.hpp"

#include <cstddef>
#include <locale>
#include <optional>
#include <sstream>
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
    const LoweredUnaryOperator operator_kind) noexcept
{
    switch (operator_kind) {
    case LoweredUnaryOperator::plus:
        return "+";
    case LoweredUnaryOperator::minus:
        return "-";
    case LoweredUnaryOperator::logical_not:
        return "!";
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<std::string_view> binary_operator_spelling(
    const LoweredBinaryOperator operator_kind) noexcept
{
    switch (operator_kind) {
    case LoweredBinaryOperator::logical_or:
        return "||";
    case LoweredBinaryOperator::logical_and:
        return "&&";
    case LoweredBinaryOperator::equal:
        return "==";
    case LoweredBinaryOperator::not_equal:
        return "!=";
    case LoweredBinaryOperator::less:
        return "<";
    case LoweredBinaryOperator::less_equal:
        return "<=";
    case LoweredBinaryOperator::greater:
        return ">";
    case LoweredBinaryOperator::greater_equal:
        return ">=";
    case LoweredBinaryOperator::add:
        return "+";
    case LoweredBinaryOperator::subtract:
        return "-";
    case LoweredBinaryOperator::multiply:
        return "*";
    case LoweredBinaryOperator::divide:
        return "/";
    case LoweredBinaryOperator::remainder:
        return "%";
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<std::string_view> assignment_operator_spelling(
    const LoweredAssignmentOperator operator_kind) noexcept
{
    switch (operator_kind) {
    case LoweredAssignmentOperator::assign:
        return "=";
    case LoweredAssignmentOperator::add_assign:
        return "+=";
    case LoweredAssignmentOperator::subtract_assign:
        return "-=";
    case LoweredAssignmentOperator::multiply_assign:
        return "*=";
    case LoweredAssignmentOperator::divide_assign:
        return "/=";
    case LoweredAssignmentOperator::remainder_assign:
        return "%=";
    }

    return std::nullopt;
}

[[nodiscard]] const LoweredIntegerLiteralExpression* unwrap_integer_literal(
    const LoweredExpression& expression,
    const TypeId expected_type)
{
    if (expression.type != expected_type) {
        return nullptr;
    }
    if (const auto* integer =
            std::get_if<LoweredIntegerLiteralExpression>(&expression.node)) {
        return integer;
    }

    const auto* grouped =
        std::get_if<LoweredGroupedExpression>(&expression.node);
    if (grouped == nullptr || grouped->expression == nullptr) {
        return nullptr;
    }

    return unwrap_integer_literal(*grouped->expression, expected_type);
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

template <typename Id>
[[nodiscard]] std::string generated_name(
    const std::string_view prefix,
    const Id id)
{
    std::ostringstream name;
    name.imbue(std::locale::classic());
    name << prefix << id.value;
    return name.str();
}

class CppGenerator {
public:
    CppGenerator(
        const TypeContext& types,
        DiagnosticEngine& diagnostics)
        : types_{types}
        , diagnostics_{diagnostics}
        , initial_error_count_{diagnostics.error_count()}
    {
        output_.imbue(std::locale::classic());
    }

    [[nodiscard]] std::optional<std::string> generate(
        const LoweredProgram& program)
    {
        validate_program(program);
        if (has_new_errors()) {
            return std::nullopt;
        }

        auto has_prototypes = false;
        for (const auto& function : program.functions) {
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

        for (std::size_t index = 0; index < program.functions.size(); ++index) {
            if (index != 0) {
                output_ << '\n';
            }
            emit_function_definition(program.functions[index]);
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

    void emit_indentation()
    {
        for (std::size_t level = 0; level < indentation_level_; ++level) {
            output_ << "    ";
        }
    }

    [[nodiscard]] bool known_type(
        const TypeId type,
        const SourceSpan span,
        const std::string_view role)
    {
        if (types_.lookup(type).has_value()) {
            return true;
        }

        report(
            span,
            "malformed lowered state: " + std::string{role}
                + " has an unknown type");
        return false;
    }

    [[nodiscard]] std::optional<std::string> cpp_type(
        const TypeId type,
        const SourceSpan span,
        const std::string_view role)
    {
        const auto descriptor = types_.lookup(type);
        if (!descriptor.has_value()) {
            report(
                span,
                "malformed lowered state: " + std::string{role}
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
                        "malformed lowered state: vector element has type "
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

        report(span, "malformed lowered state: unknown primitive type");
        return std::nullopt;
    }

    [[nodiscard]] bool is_vector_type(const TypeId type) const noexcept
    {
        const auto descriptor = types_.lookup(type);
        return descriptor.has_value()
            && std::holds_alternative<SemanticVectorType>(*descriptor);
    }

    [[nodiscard]] bool is_supported_local_type(const TypeId type) const noexcept
    {
        return type == types_.integer_type()
            || type == types_.string_type()
            || type == types_.character_type()
            || is_vector_type(type);
    }

    [[nodiscard]] std::optional<PrimitiveTypeKind> primitive_kind(
        const TypeId type) const noexcept
    {
        const auto descriptor = types_.lookup(type);
        if (!descriptor.has_value()) {
            return std::nullopt;
        }
        const auto* primitive =
            std::get_if<PrimitiveTypeKind>(&*descriptor);
        return primitive != nullptr
            ? std::optional<PrimitiveTypeKind>{*primitive}
            : std::nullopt;
    }

    [[nodiscard]] std::optional<TypeId> primitive_type(
        const PrimitiveTypeKind kind) const noexcept
    {
        switch (kind) {
        case PrimitiveTypeKind::integer:
            return types_.integer_type();
        case PrimitiveTypeKind::boolean:
            return types_.boolean_type();
        case PrimitiveTypeKind::character:
            return types_.character_type();
        case PrimitiveTypeKind::string:
            return types_.string_type();
        case PrimitiveTypeKind::void_type:
            return types_.void_type();
        }
        return std::nullopt;
    }

    void validate_program(const LoweredProgram& program)
    {
        const LoweredFunction* main_function = nullptr;

        for (const auto& function : program.functions) {
            const auto inserted =
                functions_.emplace(function.symbol.value, &function);
            if (!inserted.second) {
                report(
                    function.name_span,
                    "malformed lowered state: duplicate function symbol");
            }
            if (!seen_symbol_ids_.insert(function.symbol.value).second) {
                report(
                    function.name_span,
                    "malformed lowered state: reused semantic symbol");
            }

            if (function.is_main) {
                if (main_function != nullptr) {
                    report(
                        function.name_span,
                        "C++ code generation requires exactly one top-level "
                        "'main' function");
                } else {
                    main_function = &function;
                    main_symbol_ = function.symbol;
                }
            }
        }

        if (main_function == nullptr) {
            report(
                program.span,
                "C++ code generation requires a top-level 'int main()' "
                "function");
        }

        for (const auto& function : program.functions) {
            if (!known_type(
                    function.return_type,
                    function.name_span,
                    "function return type")) {
                continue;
            }

            if (function.is_main) {
                if (function.return_type != types_.integer_type()
                    || !function.parameters.empty()) {
                    report(
                        function.name_span,
                        "C++ code generation requires 'main' to have return "
                        "type 'int' and no parameters");
                }
            } else {
                (void)cpp_type(
                    function.return_type,
                    function.name_span,
                    "function return type");
            }

            std::unordered_set<std::size_t> parameter_ids;
            for (const auto& parameter : function.parameters) {
                if (!parameter_ids.insert(parameter.symbol.value).second
                    || !seen_symbol_ids_.insert(parameter.symbol.value).second) {
                    report(
                        parameter.name_span,
                        "malformed lowered state: duplicate parameter symbol");
                }
                const auto parameter_type = cpp_type(
                    parameter.type,
                    parameter.name_span,
                    "parameter");
                if (parameter_type.has_value() && *parameter_type == "void") {
                    report(
                        parameter.name_span,
                        "malformed lowered state: parameter has type 'void'");
                }
            }

            if (function.body == nullptr) {
                report(
                    function.span,
                    "malformed lowered state: function has no body");
            }
        }
    }

    void emit_function_signature(const LoweredFunction& function)
    {
        if (function.is_main) {
            output_ << "int main()";
            return;
        }

        const auto return_type = cpp_type(
            function.return_type,
            function.name_span,
            "function return type");
        if (!return_type.has_value()) {
            return;
        }

        output_ << *return_type << ' '
                << generated_name("tpp_function_", function.symbol) << '(';
        for (std::size_t index = 0;
             index < function.parameters.size();
             ++index) {
            if (index != 0) {
                output_ << ", ";
            }
            const auto& parameter = function.parameters[index];
            const auto parameter_type = cpp_type(
                parameter.type,
                parameter.name_span,
                "parameter");
            if (!parameter_type.has_value()) {
                continue;
            }
            output_ << *parameter_type << ' '
                    << generated_name("tpp_parameter_", parameter.symbol);
        }
        output_ << ')';
    }

    void emit_function_definition(const LoweredFunction& function)
    {
        emit_function_signature(function);
        output_ << "\n{\n";

        current_function_ = &function;
        current_parameter_types_.clear();
        current_variable_types_.clear();
        current_temporaries_.clear();
        indentation_level_ = 1;
        loop_depth_ = 0;

        for (const auto& parameter : function.parameters) {
            current_parameter_types_.emplace(
                parameter.symbol.value,
                parameter.type);
        }

        if (function.body != nullptr) {
            emit_body(*function.body);
        }

        output_ << "}\n";
        current_parameter_types_.clear();
        current_variable_types_.clear();
        current_temporaries_.clear();
        indentation_level_ = 0;
        loop_depth_ = 0;
        current_function_ = nullptr;
    }

    void emit_body(const LoweredBlock& body)
    {
        for (const auto& statement : body.statements) {
            emit_statement(statement);
        }
    }

    void emit_scoped_body(const LoweredBlock& body)
    {
        auto enclosing_variables = current_variable_types_;
        auto enclosing_temporaries = current_temporaries_;
        emit_body(body);
        current_variable_types_ = std::move(enclosing_variables);
        current_temporaries_ = std::move(enclosing_temporaries);
    }

    [[nodiscard]] std::optional<std::string> temporary_name(
        const LoweredTemporary& temporary)
    {
        switch (temporary.role) {
        case TempRole::range_begin:
            return generated_name("tpp_range_begin_", temporary.owner);
        case TempRole::range_end:
            return generated_name("tpp_range_end_", temporary.owner);
        case TempRole::range_cursor:
            return generated_name("tpp_range_cursor_", temporary.owner);
        case TempRole::range_active:
            return generated_name("tpp_range_active_", temporary.owner);
        case TempRole::iterable_snapshot:
            return generated_name("tpp_iterable_", temporary.owner);
        }

        report(
            temporary.span,
            "malformed lowered state: temporary has an unknown role");
        return std::nullopt;
    }

    [[nodiscard]] bool validate_temporary(
        const LoweredTemporary& temporary,
        const TempRole role,
        const SymbolId owner,
        const TypeId type)
    {
        auto valid = true;
        if (temporary.id.value != next_expected_temp_id_) {
            report(
                temporary.span,
                "malformed lowered state: temporary ID is not in "
                "deterministic preorder");
            valid = false;
        } else {
            ++next_expected_temp_id_;
        }
        if (!seen_temp_ids_.insert(temporary.id.value).second) {
            report(
                temporary.span,
                "malformed lowered state: duplicate temporary ID");
            valid = false;
        }
        if (temporary.role != role) {
            report(
                temporary.span,
                "malformed lowered state: temporary role does not match its "
                "loop position");
            valid = false;
        }
        if (temporary.owner != owner) {
            report(
                temporary.span,
                "malformed lowered state: temporary owner does not match its "
                "loop binding");
            valid = false;
        }
        if (temporary.type != type) {
            report(
                temporary.span,
                "malformed lowered state: temporary type does not match its "
                "loop role");
            valid = false;
        }
        if (!known_type(temporary.type, temporary.span, "temporary")
            || !temporary_name(temporary).has_value()) {
            valid = false;
        }
        return valid;
    }

    [[nodiscard]] bool activate_temporary(
        const LoweredTemporary& temporary)
    {
        const auto inserted =
            current_temporaries_.emplace(temporary.id.value, &temporary);
        if (inserted.second) {
            return true;
        }

        report(
            temporary.span,
            "malformed lowered state: temporary is already active");
        return false;
    }

    [[nodiscard]] std::optional<StorageReference> storage_reference(
        const LoweredStorage& storage,
        const SourceSpan span,
        const std::string_view role)
    {
        if (const auto* symbol = std::get_if<SymbolId>(&storage)) {
            const auto parameter =
                current_parameter_types_.find(symbol->value);
            const auto variable =
                current_variable_types_.find(symbol->value);
            if (parameter != current_parameter_types_.end()
                && variable != current_variable_types_.end()) {
                report(
                    span,
                    "malformed lowered state: storage symbol is active as "
                    "both a parameter and variable");
                return std::nullopt;
            }
            if (parameter != current_parameter_types_.end()) {
                return StorageReference{
                    .type = parameter->second,
                    .generated_name =
                        generated_name("tpp_parameter_", *symbol),
                };
            }
            if (variable != current_variable_types_.end()) {
                return StorageReference{
                    .type = variable->second,
                    .generated_name =
                        generated_name("tpp_variable_", *symbol),
                };
            }

            report(
                span,
                "malformed lowered state: " + std::string{role}
                    + " references an inactive symbol");
            return std::nullopt;
        }

        const auto temporary_id = std::get<TempId>(storage);
        const auto temporary =
            current_temporaries_.find(temporary_id.value);
        if (temporary == current_temporaries_.end()) {
            report(
                span,
                "malformed lowered state: " + std::string{role}
                    + " references an inactive temporary");
            return std::nullopt;
        }
        const auto name = temporary_name(*temporary->second);
        if (!name.has_value()) {
            return std::nullopt;
        }
        return StorageReference{
            .type = temporary->second->type,
            .generated_name = *name,
        };
    }

    [[nodiscard]] bool introduce_variable(
        const SymbolId symbol,
        const TypeId type,
        const SourceSpan span,
        const std::string_view role)
    {
        if (current_parameter_types_.contains(symbol.value)
            || current_variable_types_.contains(symbol.value)) {
            report(
                span,
                "malformed lowered state: " + std::string{role}
                    + " is already active");
            return false;
        }
        if (!seen_symbol_ids_.insert(symbol.value).second) {
            report(
                span,
                "malformed lowered state: " + std::string{role}
                    + " reuses a semantic symbol");
            return false;
        }
        current_variable_types_.emplace(symbol.value, type);
        return true;
    }

    [[nodiscard]] const LoweredExpression* unwrap_grouped_expression(
        const LoweredExpression& expression)
    {
        const auto* current = &expression;
        for (;;) {
            const auto* grouped =
                std::get_if<LoweredGroupedExpression>(&current->node);
            if (grouped == nullptr) {
                return current;
            }
            if (grouped->expression == nullptr) {
                report(
                    current->span,
                    "malformed lowered state: grouped expression has no "
                    "expression");
                return nullptr;
            }
            if (grouped->expression->type != current->type) {
                report(
                    current->span,
                    "malformed lowered state: grouped expression type does "
                    "not match its operand");
                return nullptr;
            }
            current = grouped->expression.get();
        }
    }

    void emit_statement(const LoweredStatement& statement)
    {
        std::visit(
            [this, &statement](const auto& node) {
                emit_statement_node(statement.span, node);
            },
            statement.node);
    }

    void emit_statement_node(
        const SourceSpan span,
        const LoweredVariableStatement& statement)
    {
        if (statement.initializer == nullptr) {
            report(
                span,
                "C++ code generation only supports initialized local int, "
                "string, char, or vector variables yet");
            return;
        }
        if (!known_type(statement.type, statement.name_span, "local variable")
            || !known_type(
                statement.initializer->type,
                statement.initializer->span,
                "local variable initializer")) {
            return;
        }
        if (statement.initializer->type != statement.type) {
            report(
                statement.initializer->span,
                "malformed lowered state: local variable initializer type "
                "does not match its declaration");
            return;
        }
        if (!is_supported_local_type(statement.type)) {
            report(
                statement.name_span,
                "C++ code generation only supports local variables of type "
                "'int', 'string', 'char', or 'vector<T>' yet");
            return;
        }
        if (current_parameter_types_.contains(statement.symbol.value)
            || current_variable_types_.contains(statement.symbol.value)
            || seen_symbol_ids_.contains(statement.symbol.value)) {
            report(
                statement.name_span,
                "malformed lowered state: local variable symbol is already "
                "active or reused");
            return;
        }

        const auto variable_type = cpp_type(
            statement.type,
            statement.name_span,
            "local variable");
        if (!variable_type.has_value()) {
            return;
        }

        emit_indentation();
        output_ << *variable_type << ' '
                << generated_name("tpp_variable_", statement.symbol) << " = ";
        if (!emit_expression(*statement.initializer)) {
            output_ << ";\n";
            return;
        }
        output_ << ";\n";
        (void)introduce_variable(
            statement.symbol,
            statement.type,
            statement.name_span,
            "local variable");
    }

    [[nodiscard]] bool validate_assignment_target(
        const LoweredAssignmentTarget& target,
        const StorageReference& storage)
    {
        if (storage.type != target.storage_type) {
            report(
                target.storage_span,
                "malformed lowered state: assignment storage type does not "
                "match its active storage");
            return false;
        }
        if (!known_type(
                target.storage_type,
                target.storage_span,
                "assignment storage")
            || !known_type(target.type, target.span, "assignment target")) {
            return false;
        }
        if (target.indices.size() != target.container_types.size()) {
            report(
                target.span,
                "malformed lowered state: assignment target container chain "
                "does not match its indices");
            return false;
        }

        auto current_type = target.storage_type;
        for (std::size_t index = 0; index < target.indices.size(); ++index) {
            if (target.container_types[index] != current_type) {
                report(
                    target.span,
                    "malformed lowered state: assignment target has an "
                    "invalid indexed container chain");
                return false;
            }
            const auto& index_expression = target.indices[index];
            if (index_expression == nullptr) {
                report(
                    target.span,
                    "malformed lowered state: assignment target index is "
                    "missing");
                return false;
            }
            if (index_expression->type != types_.integer_type()) {
                report(
                    index_expression->span,
                    "malformed lowered state: assignment target index does "
                    "not have type 'int'");
                return false;
            }

            if (current_type == types_.string_type()) {
                current_type = types_.character_type();
                continue;
            }

            const auto descriptor = types_.lookup(current_type);
            const auto* vector = descriptor.has_value()
                ? std::get_if<SemanticVectorType>(&*descriptor)
                : nullptr;
            if (vector == nullptr) {
                report(
                    target.span,
                    "malformed lowered state: assignment target indexes a "
                    "non-indexable type");
                return false;
            }
            current_type = vector->element_type;
        }

        if (current_type != target.type) {
            report(
                target.span,
                "malformed lowered state: assignment target type does not "
                "match its indexed storage type");
            return false;
        }
        return true;
    }

    [[nodiscard]] bool emit_assignment_target(
        const LoweredAssignmentTarget& target,
        const StorageReference& storage,
        const std::size_t depth)
    {
        if (depth == 0) {
            output_ << storage.generated_name;
            return true;
        }

        const auto container_type = target.container_types[depth - 1];
        if (container_type == types_.string_type()) {
            output_ << "tpp::runtime::string_index(";
        } else {
            const auto descriptor = types_.lookup(container_type);
            if (!descriptor.has_value()
                || !std::holds_alternative<SemanticVectorType>(*descriptor)) {
                report(
                    target.span,
                    "malformed lowered state: assignment target has an "
                    "invalid indexed container type");
                return false;
            }
            uses_vector_ = true;
            output_ << "tpp::runtime::vector_index(";
        }
        uses_runtime_ = true;

        if (!emit_assignment_target(target, storage, depth - 1)) {
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
        const LoweredAssignmentStatement& statement)
    {
        if (statement.value == nullptr) {
            report(span, "malformed lowered state: assignment has no value");
            return;
        }
        const auto storage = storage_reference(
            statement.target.storage,
            statement.target.storage_span,
            "assignment target");
        if (!storage.has_value()
            || !validate_assignment_target(statement.target, *storage)) {
            return;
        }
        if (!known_type(
                statement.value->type,
                statement.value->span,
                "assignment value")) {
            return;
        }
        if (statement.value->type != statement.target.type) {
            report(
                statement.value->span,
                "malformed lowered state: assignment value type does not "
                "match its target");
            return;
        }

        const auto spelling =
            assignment_operator_spelling(statement.operator_kind);
        if (!spelling.has_value()) {
            report(span, "malformed lowered state: unknown assignment operator");
            return;
        }

        const auto direct_assignment =
            statement.operator_kind == LoweredAssignmentOperator::assign;
        const auto integer_compound =
            statement.target.type == types_.integer_type();
        const auto string_addition =
            statement.operator_kind == LoweredAssignmentOperator::add_assign
            && statement.target.type == types_.string_type();
        const auto indexed_target = !statement.target.indices.empty();
        const auto supported_direct_assignment = !indexed_target
            && ((direct_assignment
                    && (statement.target.type == types_.string_type()
                        || statement.target.type == types_.character_type()
                        || is_vector_type(statement.target.type)))
                || string_addition);
        const auto supported_indexed_assignment = indexed_target
            && (direct_assignment || integer_compound || string_addition);
        if (!supported_direct_assignment
            && !supported_indexed_assignment) {
            report(
                span,
                indexed_target
                    ? "malformed lowered state: assignment operator is "
                      "incompatible with its indexed target type"
                    : "C++ code generation only supports '=' for string, "
                      "char, and vector assignments and '+=' for string "
                      "assignments yet");
            return;
        }
        if (statement.target.type == types_.void_type()) {
            report(
                statement.target.span,
                "malformed lowered state: assignment target has type 'void'");
            return;
        }

        emit_indentation();
        if (!emit_assignment_target(
                statement.target,
                *storage,
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
        const LoweredExpressionStatement& statement)
    {
        if (statement.expression == nullptr) {
            report(
                span,
                "malformed lowered state: expression statement has no "
                "expression");
            return;
        }

        const auto* ungrouped =
            unwrap_grouped_expression(*statement.expression);
        if (ungrouped == nullptr) {
            return;
        }
        const auto* builtin =
            std::get_if<LoweredBuiltinCallExpression>(&ungrouped->node);
        if (builtin != nullptr
            && builtin->builtin == BuiltinFunctionKind::print) {
            emit_print_statement(*statement.expression, *builtin);
            return;
        }

        const auto is_call =
            std::holds_alternative<LoweredUserCallExpression>(ungrouped->node)
            || std::holds_alternative<LoweredBuiltinCallExpression>(
                ungrouped->node)
            || std::holds_alternative<LoweredMemberCallExpression>(
                ungrouped->node);
        if (!is_call) {
            report(
                statement.expression->span,
                "C++ code generation only supports function calls as "
                "expression statements");
            return;
        }

        emit_indentation();
        if (!emit_expression(*statement.expression)) {
            output_ << '\n';
            return;
        }
        output_ << ";\n";
    }

    void emit_statement_node(
        const SourceSpan span,
        const LoweredReturnStatement& statement)
    {
        if (current_function_ == nullptr) {
            report(
                span,
                "malformed lowered state: return is outside a function");
            return;
        }

        if (statement.value == nullptr) {
            if (current_function_->return_type != types_.void_type()) {
                report(
                    span,
                    current_function_->is_main
                        ? "C++ code generation requires a value in a 'main' "
                          "return statement"
                        : "malformed lowered state: non-void return has no "
                          "value");
                return;
            }
            emit_indentation();
            output_ << "return;\n";
            return;
        }

        if (current_function_->return_type == types_.void_type()) {
            report(
                statement.value->span,
                "malformed lowered state: void return has a value");
            return;
        }
        if (statement.value->type != current_function_->return_type) {
            report(
                statement.value->span,
                "malformed lowered state: return value type does not match "
                "its function");
            return;
        }

        if (current_function_->is_main) {
            emit_indentation();
            output_ << "return static_cast<int>(";
            if (!emit_expression(*statement.value)) {
                output_ << ");\n";
                return;
            }
            output_ << ");\n";
            return;
        }

        emit_indentation();
        output_ << "return ";
        if (!emit_expression(*statement.value)) {
            output_ << ";\n";
            return;
        }
        output_ << ";\n";
    }

    void emit_statement_node(
        const SourceSpan span,
        const LoweredBreakStatement&)
    {
        if (loop_depth_ == 0) {
            report(
                span,
                "malformed lowered state: break statement is outside a loop");
            return;
        }
        emit_indentation();
        output_ << "break;\n";
    }

    void emit_statement_node(
        const SourceSpan span,
        const LoweredContinueStatement&)
    {
        if (loop_depth_ == 0) {
            report(
                span,
                "malformed lowered state: continue statement is outside a "
                "loop");
            return;
        }
        emit_indentation();
        output_ << "continue;\n";
    }

    void emit_statement_node(
        const SourceSpan span,
        const LoweredBlockStatement& statement)
    {
        if (loop_depth_ == 0) {
            report(
                span,
                "C++ code generation does not support nested blocks outside "
                "loops yet");
            return;
        }
        if (statement.block == nullptr) {
            report(
                span,
                "malformed lowered state: nested block is missing its body");
            return;
        }

        emit_indentation();
        output_ << "{\n";
        ++indentation_level_;
        emit_scoped_body(*statement.block);
        --indentation_level_;
        emit_indentation();
        output_ << "}\n";
    }

    [[nodiscard]] bool validate_range(
        const SourceSpan span,
        const LoweredRangeStatement& statement)
    {
        auto valid = true;
        if (statement.binding_type != types_.integer_type()) {
            report(
                statement.binding_span,
                "malformed lowered state: for-range binding does not have "
                "type 'int'");
            valid = false;
        }
        if (statement.begin_value == nullptr
            || statement.end_value == nullptr) {
            report(
                span,
                "malformed lowered state: for-range statement is missing a "
                "bound");
            valid = false;
        } else {
            if (statement.begin_value->type != types_.integer_type()) {
                report(
                    statement.begin_value->span,
                    "malformed lowered state: for-range begin bound does not "
                    "have type 'int'");
                valid = false;
            }
            if (statement.end_value->type != types_.integer_type()) {
                report(
                    statement.end_value->span,
                    "malformed lowered state: for-range end bound does not "
                    "have type 'int'");
                valid = false;
            }
        }
        if (statement.body == nullptr) {
            report(
                span,
                "malformed lowered state: for-range statement is missing a "
                "body");
            valid = false;
        }
        if (current_parameter_types_.contains(statement.binding.value)
            || current_variable_types_.contains(statement.binding.value)
            || seen_symbol_ids_.contains(statement.binding.value)) {
            report(
                statement.binding_span,
                "malformed lowered state: for-range binding symbol is "
                "already active or reused");
            valid = false;
        }

        valid = validate_temporary(
                    statement.begin_storage,
                    TempRole::range_begin,
                    statement.binding,
                    types_.integer_type())
            && valid;
        valid = validate_temporary(
                    statement.end_storage,
                    TempRole::range_end,
                    statement.binding,
                    types_.integer_type())
            && valid;
        valid = validate_temporary(
                    statement.cursor_storage,
                    TempRole::range_cursor,
                    statement.binding,
                    types_.integer_type())
            && valid;

        const auto exclusive =
            statement.condition_kind
                == LoweredRangeConditionKind::cursor_less_than_end
            && statement.step_kind
                == LoweredRangeStepKind::increment_cursor;
        const auto inclusive =
            statement.condition_kind == LoweredRangeConditionKind::active
            && statement.step_kind
                == LoweredRangeStepKind::update_active_then_guarded_increment;

        if (exclusive) {
            if (statement.active_storage.has_value()) {
                report(
                    statement.active_storage->span,
                    "malformed lowered state: exclusive range unexpectedly "
                    "has active storage");
                valid = false;
                valid = validate_temporary(
                            *statement.active_storage,
                            TempRole::range_active,
                            statement.binding,
                            types_.boolean_type())
                    && valid;
            }
        } else if (inclusive) {
            if (!statement.active_storage.has_value()) {
                report(
                    span,
                    "malformed lowered state: inclusive range has no active "
                    "storage");
                valid = false;
            } else {
                valid = validate_temporary(
                            *statement.active_storage,
                            TempRole::range_active,
                            statement.binding,
                            types_.boolean_type())
                    && valid;
            }
        } else {
            report(
                span,
                "malformed lowered state: inconsistent range condition and "
                "step");
            valid = false;
            if (statement.active_storage.has_value()) {
                valid = validate_temporary(
                            *statement.active_storage,
                            TempRole::range_active,
                            statement.binding,
                            types_.boolean_type())
                    && valid;
            }
        }

        return valid;
    }

    void emit_statement_node(
        const SourceSpan span,
        const LoweredRangeStatement& statement)
    {
        if (!validate_range(span, statement)) {
            return;
        }

        const auto begin_name = temporary_name(statement.begin_storage);
        const auto end_name = temporary_name(statement.end_storage);
        const auto cursor_name = temporary_name(statement.cursor_storage);
        const auto active_name = statement.active_storage.has_value()
            ? temporary_name(*statement.active_storage)
            : std::optional<std::string>{};
        if (!begin_name.has_value() || !end_name.has_value()
            || !cursor_name.has_value()
            || (statement.active_storage.has_value()
                && !active_name.has_value())) {
            return;
        }

        auto enclosing_variables = current_variable_types_;
        auto enclosing_temporaries = current_temporaries_;

        emit_indentation();
        output_ << "{\n";
        ++indentation_level_;

        emit_indentation();
        output_ << "const std::int64_t " << *begin_name << " = ";
        (void)emit_expression(*statement.begin_value);
        output_ << ";\n";
        (void)activate_temporary(statement.begin_storage);

        emit_indentation();
        output_ << "const std::int64_t " << *end_name << " = ";
        (void)emit_expression(*statement.end_value);
        output_ << ";\n";
        (void)activate_temporary(statement.end_storage);

        if (statement.active_storage.has_value()) {
            emit_indentation();
            output_ << "bool " << *active_name << " = " << *begin_name
                    << " <= " << *end_name << ";\n";
            (void)activate_temporary(*statement.active_storage);
        }

        emit_indentation();
        output_ << "for (std::int64_t " << *cursor_name << " = "
                << *begin_name << "; ";
        if (statement.condition_kind
            == LoweredRangeConditionKind::cursor_less_than_end) {
            output_ << *cursor_name << " < " << *end_name << "; ++"
                    << *cursor_name;
        } else {
            output_ << *active_name << "; " << *active_name << " = "
                    << *cursor_name << " != " << *end_name << ", "
                    << *cursor_name << " += " << *active_name
                    << " ? std::int64_t{1} : std::int64_t{0}";
        }
        output_ << ")\n";
        emit_indentation();
        output_ << "{\n";
        ++indentation_level_;
        (void)activate_temporary(statement.cursor_storage);

        emit_indentation();
        output_ << "[[maybe_unused]] std::int64_t "
                << generated_name("tpp_variable_", statement.binding)
                << " = " << *cursor_name << ";\n";

        (void)introduce_variable(
            statement.binding,
            statement.binding_type,
            statement.binding_span,
            "for-range binding");
        ++loop_depth_;
        emit_scoped_body(*statement.body);
        --loop_depth_;

        --indentation_level_;
        emit_indentation();
        output_ << "}\n";
        --indentation_level_;
        emit_indentation();
        output_ << "}\n";

        current_variable_types_ = std::move(enclosing_variables);
        current_temporaries_ = std::move(enclosing_temporaries);
    }

    [[nodiscard]] bool validate_foreach(
        const SourceSpan span,
        const LoweredForEachStatement& statement)
    {
        auto valid = true;
        if (statement.iterable == nullptr) {
            report(
                span,
                "malformed lowered state: for-each statement is missing an "
                "iterable");
            valid = false;
        } else if (statement.iterable->type != statement.iterable_type) {
            report(
                statement.iterable->span,
                "malformed lowered state: for-each iterable type does not "
                "match its expression");
            valid = false;
        }
        if (statement.body == nullptr) {
            report(
                span,
                "malformed lowered state: for-each statement is missing a "
                "body");
            valid = false;
        }
        if (current_parameter_types_.contains(statement.binding.value)
            || current_variable_types_.contains(statement.binding.value)
            || seen_symbol_ids_.contains(statement.binding.value)) {
            report(
                statement.binding_span,
                "malformed lowered state: for-each binding symbol is already "
                "active or reused");
            valid = false;
        }

        auto expected_binding = std::optional<TypeId>{};
        if (statement.iterable_type == types_.string_type()) {
            expected_binding = types_.character_type();
        } else {
            const auto descriptor = types_.lookup(statement.iterable_type);
            const auto* vector = descriptor.has_value()
                ? std::get_if<SemanticVectorType>(&*descriptor)
                : nullptr;
            if (vector != nullptr) {
                expected_binding = vector->element_type;
            }
        }
        if (!expected_binding.has_value()) {
            report(
                statement.iterable != nullptr
                    ? statement.iterable->span
                    : span,
                "malformed lowered state: for-each iterable is not a string "
                "or vector");
            valid = false;
        } else if (statement.binding_type != *expected_binding) {
            report(
                statement.binding_span,
                "malformed lowered state: for-each binding type does not "
                "match its iterable");
            valid = false;
        }

        valid = validate_temporary(
                    statement.snapshot_storage,
                    TempRole::iterable_snapshot,
                    statement.binding,
                    statement.iterable_type)
            && valid;
        return valid;
    }

    void emit_statement_node(
        const SourceSpan span,
        const LoweredForEachStatement& statement)
    {
        if (!validate_foreach(span, statement)) {
            return;
        }

        const auto iterable_type = cpp_type(
            statement.iterable_type,
            statement.iterable->span,
            "for-each iterable");
        const auto binding_type = cpp_type(
            statement.binding_type,
            statement.binding_span,
            "for-each binding");
        const auto snapshot_name = temporary_name(statement.snapshot_storage);
        if (!iterable_type.has_value() || !binding_type.has_value()
            || !snapshot_name.has_value()
            || *iterable_type == "void" || *binding_type == "void") {
            return;
        }

        auto enclosing_variables = current_variable_types_;
        auto enclosing_temporaries = current_temporaries_;

        emit_indentation();
        output_ << "{\n";
        ++indentation_level_;

        emit_indentation();
        output_ << "const " << *iterable_type << ' ' << *snapshot_name
                << " = ";
        (void)emit_expression(*statement.iterable);
        output_ << ";\n";
        (void)activate_temporary(statement.snapshot_storage);

        emit_indentation();
        output_ << "for ([[maybe_unused]] " << *binding_type << ' '
                << generated_name("tpp_variable_", statement.binding)
                << " : " << *snapshot_name << ")\n";
        emit_indentation();
        output_ << "{\n";
        ++indentation_level_;

        (void)introduce_variable(
            statement.binding,
            statement.binding_type,
            statement.binding_span,
            "for-each binding");
        ++loop_depth_;
        emit_scoped_body(*statement.body);
        --loop_depth_;

        --indentation_level_;
        emit_indentation();
        output_ << "}\n";
        --indentation_level_;
        emit_indentation();
        output_ << "}\n";

        current_variable_types_ = std::move(enclosing_variables);
        current_temporaries_ = std::move(enclosing_temporaries);
    }

    void emit_print_statement(
        const LoweredExpression& statement_expression,
        const LoweredBuiltinCallExpression& call)
    {
        if (statement_expression.type != types_.void_type()
            || call.arguments.size() != 1
            || call.arguments.front() == nullptr) {
            report(
                statement_expression.span,
                "malformed lowered state: builtin 'print' call does not "
                "match its signature");
            return;
        }
        const auto argument_kind =
            primitive_kind(call.arguments.front()->type);
        const auto signature =
            builtin_function_signature(BuiltinFunctionKind::print);
        if (signature.parameter_types.size() != 1
            || !argument_kind.has_value()
            || !builtin_parameter_accepts(
                signature.parameter_types.front(),
                *argument_kind)) {
            report(
                statement_expression.span,
                "malformed lowered state: builtin 'print' call does not "
                "match its signature");
            return;
        }

        emit_indentation();
        output_ << "std::cout << std::boolalpha << ";
        if (!emit_expression(*call.arguments.front())) {
            output_ << '\n';
            return;
        }
        output_ << " << '\\n';\n";
    }

    [[nodiscard]] bool emit_expression(const LoweredExpression& expression)
    {
        if (!known_type(expression.type, expression.span, "expression")) {
            return false;
        }
        return std::visit(
            [this, &expression](const auto& node) {
                return emit_expression_node(expression, node);
            },
            expression.node);
    }

    [[nodiscard]] bool emit_expression_node(
        const LoweredExpression& expression,
        const LoweredIntegerLiteralExpression& literal)
    {
        if (expression.type != types_.integer_type()) {
            report(
                expression.span,
                "malformed lowered state: integer literal has a non-integer "
                "type");
            return false;
        }
        const auto normalized = normalize_integer_lexeme(literal.lexeme);
        if (!normalized.has_value()) {
            report(
                expression.span,
                "malformed lowered state: invalid integer literal lexeme");
            return false;
        }
        if (exceeds_magnitude(*normalized, maximum_integer_magnitude)) {
            report(
                expression.span,
                "integer literal is outside the supported signed 64-bit "
                "code-generation range");
            return false;
        }

        output_ << "std::int64_t{" << *normalized << '}';
        return true;
    }

    [[nodiscard]] bool emit_expression_node(
        const LoweredExpression& expression,
        const LoweredBooleanLiteralExpression& literal)
    {
        if (expression.type != types_.boolean_type()) {
            report(
                expression.span,
                "malformed lowered state: boolean literal has a non-boolean "
                "type");
            return false;
        }
        output_ << (literal.value ? "true" : "false");
        return true;
    }

    [[nodiscard]] bool emit_expression_node(
        const LoweredExpression& expression,
        const LoweredCharacterLiteralExpression& literal)
    {
        if (expression.type != types_.character_type()) {
            report(
                expression.span,
                "malformed lowered state: character literal has a non-char "
                "type");
            return false;
        }
        output_ << '\''
                << escape_cpp_bytes(
                       std::string_view{&literal.value, 1},
                       '\'')
                << '\'';
        return true;
    }

    [[nodiscard]] bool emit_expression_node(
        const LoweredExpression& expression,
        const LoweredStringLiteralExpression& literal)
    {
        if (expression.type != types_.string_type()) {
            report(
                expression.span,
                "malformed lowered state: string literal has a non-string "
                "type");
            return false;
        }
        output_ << "std::string{\""
                << escape_cpp_bytes(literal.value, '"')
                << "\", " << literal.value.size() << '}';
        return true;
    }

    [[nodiscard]] bool emit_expression_node(
        const LoweredExpression& expression,
        const LoweredStorageExpression& storage_expression)
    {
        const auto storage = storage_reference(
            storage_expression.storage,
            expression.span,
            "storage expression");
        if (!storage.has_value()) {
            return false;
        }
        if (storage->type != expression.type) {
            report(
                expression.span,
                "malformed lowered state: storage expression type does not "
                "match its storage");
            return false;
        }
        output_ << storage->generated_name;
        return true;
    }

    [[nodiscard]] bool emit_expression_node(
        const LoweredExpression& expression,
        const LoweredUnaryExpression& unary)
    {
        if (unary.operand == nullptr) {
            report(
                expression.span,
                "malformed lowered state: unary expression has no operand");
            return false;
        }
        const auto spelling = unary_operator_spelling(unary.operator_kind);
        if (!spelling.has_value()) {
            report(
                expression.span,
                "malformed lowered state: unknown unary operator");
            return false;
        }

        const auto expected_type =
            unary.operator_kind == LoweredUnaryOperator::logical_not
            ? types_.boolean_type()
            : types_.integer_type();
        if (expression.type != expected_type
            || unary.operand->type != expected_type) {
            report(
                expression.span,
                "malformed lowered state: unary expression types do not "
                "match its operator");
            return false;
        }

        if (unary.operator_kind == LoweredUnaryOperator::minus) {
            const auto* integer = unwrap_integer_literal(
                *unary.operand,
                types_.integer_type());
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

        output_ << '(' << *spelling;
        if (!emit_expression(*unary.operand)) {
            output_ << ')';
            return false;
        }
        output_ << ')';
        return true;
    }

    [[nodiscard]] bool validate_binary_types(
        const LoweredExpression& expression,
        const LoweredBinaryExpression& binary)
    {
        const auto left_type = binary.left->type;
        const auto right_type = binary.right->type;
        auto valid = false;

        switch (binary.operator_kind) {
        case LoweredBinaryOperator::add:
            valid = left_type == right_type
                && (left_type == types_.integer_type()
                    || left_type == types_.string_type())
                && expression.type == left_type;
            break;
        case LoweredBinaryOperator::subtract:
        case LoweredBinaryOperator::multiply:
        case LoweredBinaryOperator::divide:
        case LoweredBinaryOperator::remainder:
            valid = left_type == types_.integer_type()
                && right_type == types_.integer_type()
                && expression.type == types_.integer_type();
            break;
        case LoweredBinaryOperator::logical_or:
        case LoweredBinaryOperator::logical_and:
            valid = left_type == types_.boolean_type()
                && right_type == types_.boolean_type()
                && expression.type == types_.boolean_type();
            break;
        case LoweredBinaryOperator::less:
        case LoweredBinaryOperator::less_equal:
        case LoweredBinaryOperator::greater:
        case LoweredBinaryOperator::greater_equal:
            valid = left_type == right_type
                && (left_type == types_.integer_type()
                    || left_type == types_.string_type())
                && expression.type == types_.boolean_type();
            break;
        case LoweredBinaryOperator::equal:
        case LoweredBinaryOperator::not_equal: {
            const auto primitive = primitive_kind(left_type);
            valid = left_type == right_type
                && primitive.has_value()
                && *primitive != PrimitiveTypeKind::void_type
                && expression.type == types_.boolean_type();
            break;
        }
        }

        if (!valid) {
            report(
                expression.span,
                "malformed lowered state: binary expression types do not "
                "match its operator");
        }
        return valid;
    }

    [[nodiscard]] bool emit_expression_node(
        const LoweredExpression& expression,
        const LoweredBinaryExpression& binary)
    {
        if (binary.left == nullptr || binary.right == nullptr) {
            report(
                expression.span,
                "malformed lowered state: binary expression is missing an "
                "operand");
            return false;
        }
        const auto spelling =
            binary_operator_spelling(binary.operator_kind);
        if (!spelling.has_value()) {
            report(
                expression.span,
                "malformed lowered state: unknown binary operator");
            return false;
        }
        if (!validate_binary_types(expression, binary)) {
            return false;
        }

        output_ << '(';
        if (!emit_expression(*binary.left)) {
            output_ << ')';
            return false;
        }
        output_ << ' ' << *spelling << ' ';
        if (!emit_expression(*binary.right)) {
            output_ << ')';
            return false;
        }
        output_ << ')';
        return true;
    }

    [[nodiscard]] bool emit_expression_node(
        const LoweredExpression& expression,
        const LoweredUserCallExpression& call)
    {
        const auto target = functions_.find(call.function.value);
        if (target == functions_.end()) {
            report(
                call.callee_span,
                "malformed lowered state: user call has an unknown function "
                "symbol");
            return false;
        }
        const auto& function = *target->second;
        if (function.is_main
            || (main_symbol_.has_value() && call.function == *main_symbol_)) {
            report(
                call.callee_span,
                "C++ code generation cannot call 'main'");
            return false;
        }
        if (expression.type != function.return_type
            || call.arguments.size() != function.parameters.size()) {
            report(
                expression.span,
                "malformed lowered state: user call does not match its "
                "function signature");
            return false;
        }
        for (std::size_t index = 0; index < call.arguments.size(); ++index) {
            const auto& argument = call.arguments[index];
            if (argument == nullptr
                || argument->type != function.parameters[index].type) {
                report(
                    argument != nullptr ? argument->span : expression.span,
                    "malformed lowered state: user call argument does not "
                    "match its function signature");
                return false;
            }
        }

        output_ << generated_name("tpp_function_", call.function) << '(';
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
        const LoweredExpression& expression,
        const LoweredBuiltinCallExpression& call)
    {
        switch (call.builtin) {
        case BuiltinFunctionKind::print:
            report(
                expression.span,
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
            report(
                call.callee_span,
                "malformed lowered state: unknown builtin function");
            return false;
        }

        const auto signature = builtin_function_signature(call.builtin);
        const auto result_type = primitive_type(signature.return_type);
        if (!result_type.has_value() || expression.type != *result_type
            || call.arguments.size() != signature.parameter_types.size()) {
            report(
                expression.span,
                "malformed lowered state: builtin '"
                    + std::string{builtin_name(call.builtin)}
                    + "' does not match its signature");
            return false;
        }
        for (std::size_t index = 0; index < call.arguments.size(); ++index) {
            const auto& argument = call.arguments[index];
            const auto argument_kind = argument != nullptr
                ? primitive_kind(argument->type)
                : std::nullopt;
            if (argument == nullptr || !argument_kind.has_value()
                || !builtin_parameter_accepts(
                    signature.parameter_types[index],
                    *argument_kind)) {
                report(
                    argument != nullptr ? argument->span : expression.span,
                    "malformed lowered state: builtin argument type does not "
                    "match its signature");
                return false;
            }
        }

        uses_runtime_ = true;
        switch (call.builtin) {
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

        report(
            call.callee_span,
            "malformed lowered state: unknown builtin function");
        return false;
    }

    [[nodiscard]] bool emit_expression_node(
        const LoweredExpression& expression,
        const LoweredMemberCallExpression& call)
    {
        if (call.receiver == nullptr) {
            report(
                expression.span,
                "malformed lowered state: string member has no receiver");
            return false;
        }
        if (call.receiver->type != types_.string_type()) {
            report(
                call.receiver->span,
                "malformed lowered state: string member receiver does not "
                "have type 'string'");
            return false;
        }

        auto expected_arity = std::size_t{0};
        auto expected_result = types_.integer_type();
        auto helper = std::string_view{"tpp::runtime::string_length("};
        switch (call.member) {
        case MemberKind::string_length:
            break;
        case MemberKind::string_push:
            expected_arity = 1;
            expected_result = types_.void_type();
            helper = "tpp::runtime::string_push(";
            break;
        default:
            report(
                call.member_span,
                "malformed lowered state: unknown string member identity");
            return false;
        }
        if (expression.type != expected_result
            || call.arguments.size() != expected_arity) {
            report(
                expression.span,
                "malformed lowered state: string member call does not match "
                "its member signature");
            return false;
        }
        if (call.member == MemberKind::string_push
            && (call.arguments.front() == nullptr
                || call.arguments.front()->type
                    != types_.character_type())) {
            report(
                expression.span,
                "malformed lowered state: string push argument does not have "
                "type 'char'");
            return false;
        }

        uses_runtime_ = true;
        output_ << helper;
        if (!emit_expression(*call.receiver)) {
            output_ << ')';
            return false;
        }
        if (call.member == MemberKind::string_push) {
            output_ << ", ";
            if (!emit_expression(*call.arguments.front())) {
                output_ << ')';
                return false;
            }
        }
        output_ << ')';
        return true;
    }

    [[nodiscard]] bool emit_expression_node(
        const LoweredExpression& expression,
        const LoweredIndexExpression& index)
    {
        if (index.base == nullptr || index.index == nullptr) {
            report(
                expression.span,
                "malformed lowered state: index expression is missing an "
                "operand");
            return false;
        }
        if (index.container_type != index.base->type
            || index.index->type != types_.integer_type()) {
            report(
                expression.span,
                "malformed lowered state: index operand types do not match "
                "its recorded container");
            return false;
        }

        auto helper = std::string_view{};
        auto expected_result = std::optional<TypeId>{};
        if (index.container_type == types_.string_type()) {
            helper = "tpp::runtime::string_index(";
            expected_result = types_.character_type();
        } else {
            const auto descriptor = types_.lookup(index.container_type);
            const auto* vector = descriptor.has_value()
                ? std::get_if<SemanticVectorType>(&*descriptor)
                : nullptr;
            if (vector == nullptr) {
                report(
                    index.base->span,
                    "malformed lowered state: index base is not a string or "
                    "vector");
                return false;
            }
            helper = "tpp::runtime::vector_index(";
            expected_result = vector->element_type;
            uses_vector_ = true;
        }
        if (!expected_result.has_value()
            || expression.type != *expected_result) {
            report(
                expression.span,
                "malformed lowered state: index result type does not match "
                "its base type");
            return false;
        }

        uses_runtime_ = true;
        output_ << helper;
        if (!emit_expression(*index.base)) {
            output_ << ')';
            return false;
        }
        output_ << ", ";
        if (!emit_expression(*index.index)) {
            output_ << ')';
            return false;
        }
        output_ << ')';
        return true;
    }

    [[nodiscard]] bool emit_expression_node(
        const LoweredExpression& expression,
        const LoweredVectorConstructionExpression& construction)
    {
        const auto descriptor = types_.lookup(expression.type);
        const auto* vector = descriptor.has_value()
            ? std::get_if<SemanticVectorType>(&*descriptor)
            : nullptr;
        if (vector == nullptr
            || vector->element_type != construction.element_type) {
            report(
                expression.span,
                "malformed lowered state: vector construction type does not "
                "match its element type");
            return false;
        }
        if (construction.arguments.size() > 2) {
            report(
                expression.span,
                "malformed lowered state: vector construction has an "
                "unexpected number of arguments");
            return false;
        }
        for (std::size_t index = 0;
             index < construction.arguments.size();
             ++index) {
            const auto& argument = construction.arguments[index];
            const auto expected_type = index == 0
                ? types_.integer_type()
                : construction.element_type;
            if (argument == nullptr || argument->type != expected_type) {
                report(
                    argument != nullptr ? argument->span : expression.span,
                    "malformed lowered state: vector construction argument "
                    "type does not match its position");
                return false;
            }
        }

        const auto vector_type = cpp_type(
            expression.type,
            expression.span,
            "vector construction");
        if (!vector_type.has_value()) {
            return false;
        }
        if (construction.arguments.empty()) {
            output_ << *vector_type << "{}";
            return true;
        }

        const auto element_type = cpp_type(
            construction.element_type,
            expression.span,
            "vector element");
        if (!element_type.has_value() || *element_type == "void") {
            return false;
        }
        uses_runtime_ = true;
        output_ << "tpp::runtime::make_vector<" << *element_type << ">(";
        auto valid = true;
        for (std::size_t index = 0;
             index < construction.arguments.size();
             ++index) {
            if (index != 0) {
                output_ << ", ";
            }
            if (!emit_expression(*construction.arguments[index])) {
                valid = false;
            }
        }
        output_ << ')';
        return valid;
    }

    [[nodiscard]] bool emit_expression_node(
        const LoweredExpression& expression,
        const LoweredGroupedExpression& grouped)
    {
        if (grouped.expression == nullptr) {
            report(
                expression.span,
                "malformed lowered state: grouped expression has no "
                "expression");
            return false;
        }
        if (grouped.expression->type != expression.type) {
            report(
                expression.span,
                "malformed lowered state: grouped expression type does not "
                "match its operand");
            return false;
        }

        output_ << '(';
        if (!emit_expression(*grouped.expression)) {
            output_ << ')';
            return false;
        }
        output_ << ')';
        return true;
    }

    const TypeContext& types_;
    DiagnosticEngine& diagnostics_;
    std::size_t initial_error_count_;
    std::ostringstream output_;
    std::unordered_map<std::size_t, const LoweredFunction*> functions_;
    std::optional<SymbolId> main_symbol_;
    const LoweredFunction* current_function_{nullptr};
    std::unordered_map<std::size_t, TypeId> current_parameter_types_;
    std::unordered_map<std::size_t, TypeId> current_variable_types_;
    std::unordered_map<std::size_t, const LoweredTemporary*>
        current_temporaries_;
    std::unordered_set<std::size_t> seen_symbol_ids_;
    std::unordered_set<std::size_t> seen_temp_ids_;
    std::size_t next_expected_temp_id_{0};
    std::size_t indentation_level_{0};
    std::size_t loop_depth_{0};
    bool uses_vector_{false};
    bool uses_runtime_{false};
};

}

std::optional<std::string> generate_cpp(
    const LoweredProgram& program,
    const TypeContext& types,
    DiagnosticEngine& diagnostics)
{
    if (diagnostics.has_errors()) {
        return std::nullopt;
    }

    return CppGenerator{types, diagnostics}.generate(program);
}

}

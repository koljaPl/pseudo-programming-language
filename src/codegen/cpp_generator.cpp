#include "pseudo/codegen/cpp_generator.hpp"

#include "pseudo/ast/program.hpp"
#include "pseudo/diagnostics/diagnostic_engine.hpp"

#include <cstddef>
#include <locale>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

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

[[nodiscard]] bool is_arithmetic_operator(
    const BinaryOperator operator_kind) noexcept
{
    switch (operator_kind) {
    case BinaryOperator::add:
    case BinaryOperator::subtract:
    case BinaryOperator::multiply:
    case BinaryOperator::divide:
    case BinaryOperator::remainder:
        return true;
    case BinaryOperator::logical_or:
    case BinaryOperator::logical_and:
    case BinaryOperator::equal:
    case BinaryOperator::not_equal:
    case BinaryOperator::less:
    case BinaryOperator::less_equal:
    case BinaryOperator::greater:
    case BinaryOperator::greater_equal:
        return false;
    }

    return false;
}

[[nodiscard]] bool is_integer_return_type(const ReturnType& return_type)
{
    const auto* value_type = std::get_if<ValueType>(&return_type.node);
    if (value_type == nullptr) {
        return false;
    }

    const auto* scalar_type =
        std::get_if<ScalarTypeKind>(&value_type->node);
    return scalar_type != nullptr
        && *scalar_type == ScalarTypeKind::integer;
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

[[nodiscard]] bool is_integer_return_expression(const Expression& expression)
{
    return std::visit(
        [](const auto& node) -> bool {
            using Node = std::decay_t<decltype(node)>;

            if constexpr (std::is_same_v<Node, IntegerLiteralExpression>) {
                return true;
            } else if constexpr (std::is_same_v<Node, ParenthesizedExpression>) {
                return node.expression != nullptr
                    && is_integer_return_expression(*node.expression);
            } else if constexpr (std::is_same_v<Node, UnaryExpression>) {
                return node.operand != nullptr
                    && (node.operator_kind == UnaryOperator::plus
                        || node.operator_kind == UnaryOperator::minus)
                    && is_integer_return_expression(*node.operand);
            } else if constexpr (std::is_same_v<Node, BinaryExpression>) {
                return node.left != nullptr && node.right != nullptr
                    && is_arithmetic_operator(node.operator_kind)
                    && is_integer_return_expression(*node.left)
                    && is_integer_return_expression(*node.right);
            } else {
                return false;
            }
        },
        expression.node);
}

class CppGenerator {
public:
    explicit CppGenerator(DiagnosticEngine& diagnostics)
        : diagnostics_{diagnostics}
        , initial_error_count_{diagnostics.error_count()}
    {
        output_.imbue(std::locale::classic());
    }

    [[nodiscard]] std::optional<std::string> generate(const Program& program)
    {
        const FunctionDeclaration* main_function = validate_program(program);
        if (main_function == nullptr || has_new_errors()) {
            return std::nullopt;
        }

        output_
            << "#include <cstdint>\n"
               "#include <iostream>\n"
               "#include <string>\n"
               "\n"
               "int main()\n"
               "{\n";

        emit_main_body(*main_function->body);
        output_ << "}\n";

        if (has_new_errors()) {
            return std::nullopt;
        }

        return output_.str();
    }

private:
    [[nodiscard]] bool has_new_errors() const noexcept
    {
        return diagnostics_.error_count() != initial_error_count_;
    }

    void report(const SourceSpan span, std::string message)
    {
        diagnostics_.error(span, std::move(message));
    }

    [[nodiscard]] const FunctionDeclaration* validate_program(
        const Program& program)
    {
        const FunctionDeclaration* main_function = nullptr;

        for (const auto& declaration : program.declarations) {
            std::visit(
                [&](const auto& node) {
                    using Node = std::decay_t<decltype(node)>;

                    if constexpr (std::is_same_v<Node, VariableDeclaration>) {
                        report(
                            node.span,
                            "C++ code generation does not support global variables yet");
                    } else if (node.name != "main") {
                        report(
                            node.span,
                            "C++ code generation does not support top-level "
                            "functions other than 'main' yet");
                    } else if (main_function != nullptr) {
                        report(
                            node.name_span,
                            "C++ code generation requires exactly one top-level 'main' function");
                    } else {
                        main_function = &node;
                    }
                },
                declaration);
        }

        if (main_function == nullptr) {
            report(
                program.span,
                "C++ code generation requires a top-level 'int main()' function");
            return nullptr;
        }

        if (!is_integer_return_type(main_function->return_type)
            || !main_function->parameters.empty()) {
            report(
                main_function->name_span,
                "C++ code generation requires 'main' to have return type 'int' and no parameters");
        }

        if (main_function->body == nullptr) {
            report(
                main_function->span,
                "malformed AST: 'main' function has no body");
            return nullptr;
        }

        return main_function;
    }

    void emit_main_body(const Block& body)
    {
        for (const auto& item : body.items) {
            std::visit(
                [this](const auto& node) {
                    using Node = std::decay_t<decltype(node)>;

                    if constexpr (std::is_same_v<Node, FunctionDeclaration>) {
                        report(
                            node.span,
                            "C++ code generation does not support nested functions yet");
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
        const VariableDeclaration&)
    {
        report(
            span,
            "C++ code generation does not support local variables yet");
    }

    void emit_statement_node(
        const SourceSpan span,
        const AssignmentStatement&)
    {
        report(span, "C++ code generation does not support assignments yet");
    }

    void emit_statement_node(
        const SourceSpan span,
        const ExpressionStatement& statement)
    {
        if (statement.expression == nullptr) {
            report(span, "malformed AST: expression statement has no expression");
            return;
        }

        const auto* call =
            std::get_if<CallExpression>(&statement.expression->node);
        if (call == nullptr) {
            report(
                statement.expression->span,
                "C++ code generation only supports calls to builtin 'print' "
                "as expression statements");
            return;
        }

        if (call->callee == nullptr) {
            report(statement.expression->span, "malformed AST: call has no callee");
            return;
        }

        const auto* callee =
            std::get_if<IdentifierExpression>(&call->callee->node);
        if (callee == nullptr || callee->name != "print") {
            report(
                call->callee->span,
                "C++ code generation only supports calls to builtin 'print'");
            return;
        }

        if (call->arguments.size() != 1) {
            report(
                statement.expression->span,
                "builtin 'print' expects exactly one argument");
            return;
        }

        if (call->arguments.front() == nullptr) {
            report(
                statement.expression->span,
                "malformed AST: 'print' argument is missing");
            return;
        }

        output_ << "    std::cout << std::boolalpha << ";
        if (!emit_expression(*call->arguments.front())) {
            output_ << '\n';
            return;
        }
        output_ << " << '\\n';\n";
    }

    void emit_statement_node(const SourceSpan span, const IfStatement&)
    {
        report(span, "C++ code generation does not support if statements yet");
    }

    void emit_statement_node(const SourceSpan span, const WhileStatement&)
    {
        report(span, "C++ code generation does not support while statements yet");
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
        if (statement.value == nullptr) {
            report(
                span,
                "C++ code generation requires a value in a 'main' return statement");
            return;
        }

        if (!is_integer_return_expression(*statement.value)) {
            report(
                statement.value->span,
                "C++ code generation only supports integer arithmetic in 'main' return statements");
            return;
        }

        output_ << "    return static_cast<int>(";
        if (!emit_expression(*statement.value)) {
            output_ << ");\n";
            return;
        }
        output_ << ");\n";
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

    [[nodiscard]] bool emit_expression(const Expression& expression)
    {
        return std::visit(
            [this, &expression](const auto& node) {
                return emit_expression_node(expression.span, node);
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
                "integer literal is outside the supported signed 64-bit code-generation range");
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
        const IdentifierExpression&)
    {
        report(
            span,
            "C++ code generation does not support identifier expressions yet");
        return false;
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
            const auto* integer =
                unwrap_integer_literal(*expression.operand);
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
            report(span, "malformed AST: binary expression is missing an operand");
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
        const CallExpression&)
    {
        report(span, "C++ code generation does not support nested calls yet");
        return false;
    }

    [[nodiscard]] bool emit_expression_node(
        const SourceSpan span,
        const IndexExpression&)
    {
        report(span, "C++ code generation does not support indexing yet");
        return false;
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
        const VectorConstructionExpression&)
    {
        report(
            span,
            "C++ code generation does not support vector construction yet");
        return false;
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

    DiagnosticEngine& diagnostics_;
    std::size_t initial_error_count_;
    std::ostringstream output_;
};

}

std::optional<std::string> generate_cpp(
    const Program& program,
    DiagnosticEngine& diagnostics)
{
    if (diagnostics.has_errors()) {
        return std::nullopt;
    }

    return CppGenerator{diagnostics}.generate(program);
}

}

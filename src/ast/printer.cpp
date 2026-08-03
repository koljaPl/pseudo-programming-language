#include "pseudo/ast/printer.hpp"

#include "pseudo/ast/program.hpp"

#include <cstddef>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace tpp {
namespace {

class IndentGuard {
public:
    explicit IndentGuard(std::size_t& depth) noexcept
        : depth_{depth}
    {
        ++depth_;
    }

    ~IndentGuard()
    {
        --depth_;
    }

    IndentGuard(const IndentGuard&) = delete;
    IndentGuard& operator=(const IndentGuard&) = delete;

private:
    std::size_t& depth_;
};

std::string escape_bytes(const std::string_view bytes)
{
    constexpr std::string_view hex_digits = "0123456789ABCDEF";
    std::string escaped;

    for (const char value : bytes) {
        const auto byte = static_cast<unsigned char>(value);

        switch (byte) {
        case '\0':
            escaped += "\\0";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\'':
            escaped += "\\\'";
            break;
        default:
            if (byte >= 0x20 && byte <= 0x7E) {
                escaped.push_back(static_cast<char>(byte));
            } else {
                escaped += "\\x";
                escaped.push_back(hex_digits[byte >> 4]);
                escaped.push_back(hex_digits[byte & 0x0F]);
            }
            break;
        }
    }

    return escaped;
}

std::string quote(
    const std::string_view value,
    const char delimiter)
{
    std::string quoted;
    quoted.push_back(delimiter);
    quoted += escape_bytes(value);
    quoted.push_back(delimiter);
    return quoted;
}

std::string quote_byte(const char value)
{
    return quote(std::string_view{&value, 1}, '\'');
}

std::string_view scalar_type_name(const ScalarTypeKind kind)
{
    switch (kind) {
    case ScalarTypeKind::integer:
        return "int";
    case ScalarTypeKind::boolean:
        return "bool";
    case ScalarTypeKind::character:
        return "char";
    case ScalarTypeKind::string:
        return "string";
    }

    throw std::logic_error{"unknown scalar type"};
}

std::string value_type_name(const ValueType& type)
{
    return std::visit(
        [](const auto& node) -> std::string {
            using Node = std::decay_t<decltype(node)>;

            if constexpr (std::is_same_v<Node, ScalarTypeKind>) {
                return std::string{scalar_type_name(node)};
            } else {
                return "vector<" + value_type_name(*node.element_type)
                    + '>';
            }
        },
        type.node);
}

std::string return_type_name(const ReturnType& type)
{
    return std::visit(
        [](const auto& node) -> std::string {
            using Node = std::decay_t<decltype(node)>;

            if constexpr (std::is_same_v<Node, VoidType>) {
                return "void";
            } else {
                return value_type_name(node);
            }
        },
        type.node);
}

std::string_view unary_operator_name(const UnaryOperator kind)
{
    switch (kind) {
    case UnaryOperator::plus:
        return "+";
    case UnaryOperator::minus:
        return "-";
    case UnaryOperator::logical_not:
        return "!";
    }

    throw std::logic_error{"unknown unary operator"};
}

std::string_view binary_operator_name(const BinaryOperator kind)
{
    switch (kind) {
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

    throw std::logic_error{"unknown binary operator"};
}

std::string_view assignment_operator_name(const AssignmentOperator kind)
{
    switch (kind) {
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

    throw std::logic_error{"unknown assignment operator"};
}

std::string_view range_operator_name(const RangeOperator kind)
{
    switch (kind) {
    case RangeOperator::exclusive:
        return "..";
    case RangeOperator::inclusive:
        return "..=";
    }

    throw std::logic_error{"unknown range operator"};
}

class AstPrinter {
public:
    explicit AstPrinter(std::ostream& output) noexcept
        : output_{output}
    {
    }

    void print(const Program& program)
    {
        line("Program");
        indented([&] {
            for (const auto& declaration : program.declarations) {
                print_top_level_declaration(declaration);
            }
        });
    }

private:
    template <typename Callback>
    void indented(Callback&& callback)
    {
        const IndentGuard guard{depth_};
        std::forward<Callback>(callback)();
    }

    template <typename Callback>
    void role(const std::string_view name, Callback&& callback)
    {
        line(name);
        indented(std::forward<Callback>(callback));
    }

    void write_indent()
    {
        for (std::size_t index = 0; index < depth_; ++index) {
            output_ << "  ";
        }
    }

    void line(const std::string_view text)
    {
        write_indent();
        output_ << text << '\n';
    }

    void print_top_level_declaration(
        const TopLevelDeclaration& declaration)
    {
        std::visit(
            [this](const auto& node) {
                print_top_level_node(node);
            },
            declaration);
    }

    void print_top_level_node(const FunctionDeclaration& function)
    {
        print_function(function);
    }

    void print_top_level_node(const VariableDeclaration& variable)
    {
        print_variable(variable);
    }

    void print_function(const FunctionDeclaration& function)
    {
        write_indent();
        output_ << "Function name=" << quote(function.name, '"')
                << " return=" << return_type_name(function.return_type)
                << '\n';

        indented([&] {
            line("Parameters");
            indented([&] {
                for (const auto& parameter : function.parameters) {
                    print_parameter(parameter);
                }
            });
            print_block(*function.body);
        });
    }

    void print_parameter(const Parameter& parameter)
    {
        write_indent();
        output_ << "Parameter name=" << quote(parameter.name, '"')
                << " type=" << value_type_name(parameter.type) << '\n';
    }

    void print_variable(const VariableDeclaration& variable)
    {
        write_indent();
        output_ << "Variable name=" << quote(variable.name, '"')
                << " type=" << value_type_name(variable.type) << '\n';

        if (variable.initializer) {
            indented([&] {
                role("Initializer", [&] {
                    print_expression(*variable.initializer);
                });
            });
        }
    }

    void print_block(const Block& block)
    {
        line("Block");
        indented([&] {
            for (const auto& item : block.items) {
                std::visit(
                    [this](const auto& node) {
                        print_block_item(node);
                    },
                    item);
            }
        });
    }

    void print_block_item(const FunctionDeclaration& function)
    {
        print_function(function);
    }

    void print_block_item(const Statement& statement)
    {
        print_statement(statement);
    }

    void print_statement(const Statement& statement)
    {
        std::visit(
            [this](const auto& node) {
                print_statement_node(node);
            },
            statement.node);
    }

    void print_statement_node(const VariableDeclaration& variable)
    {
        print_variable(variable);
    }

    void print_statement_node(const AssignmentStatement& assignment)
    {
        write_indent();
        output_ << "Assignment operator="
                << quote(assignment_operator_name(assignment.operator_kind), '"')
                << '\n';

        indented([&] {
            print_assignment_target(assignment.target);
            role("Value", [&] {
                print_expression(*assignment.value);
            });
        });
    }

    void print_assignment_target(const AssignmentTarget& target)
    {
        write_indent();
        output_ << "AssignmentTarget name=" << quote(target.name, '"')
                << '\n';

        indented([&] {
            line("Indices");
            indented([&] {
                for (const auto& index : target.indices) {
                    print_expression(*index);
                }
            });
        });
    }

    void print_statement_node(const ExpressionStatement& statement)
    {
        line("ExpressionStatement");
        indented([&] {
            print_expression(*statement.expression);
        });
    }

    void print_statement_node(const IfStatement& statement)
    {
        line("If");
        indented([&] {
            role("Condition", [&] {
                print_expression(*statement.condition);
            });
            role("Then", [&] {
                print_block(*statement.then_block);
            });

            if (statement.else_block) {
                role("Else", [&] {
                    print_block(*statement.else_block);
                });
            }
        });
    }

    void print_statement_node(const WhileStatement& statement)
    {
        line("While");
        indented([&] {
            role("Condition", [&] {
                print_expression(*statement.condition);
            });
            role("Body", [&] {
                print_block(*statement.body);
            });
        });
    }

    void print_statement_node(const ForRangeStatement& statement)
    {
        write_indent();
        output_ << "ForRange variable=" << quote(statement.variable, '"')
                << " operator="
                << quote(range_operator_name(statement.operator_kind), '"')
                << '\n';

        indented([&] {
            role("Begin", [&] {
                print_expression(*statement.begin);
            });
            role("End", [&] {
                print_expression(*statement.end);
            });
            role("Body", [&] {
                print_block(*statement.body);
            });
        });
    }

    void print_statement_node(const ForEachStatement& statement)
    {
        write_indent();
        output_ << "ForEach variable=" << quote(statement.variable, '"')
                << '\n';

        indented([&] {
            role("Iterable", [&] {
                print_expression(*statement.iterable);
            });
            role("Body", [&] {
                print_block(*statement.body);
            });
        });
    }

    void print_statement_node(const ReturnStatement& statement)
    {
        line("Return");
        if (statement.value) {
            indented([&] {
                print_expression(*statement.value);
            });
        }
    }

    void print_statement_node(const BreakStatement&)
    {
        line("Break");
    }

    void print_statement_node(const ContinueStatement&)
    {
        line("Continue");
    }

    void print_statement_node(const BlockStatement& statement)
    {
        line("BlockStatement");
        indented([&] {
            print_block(*statement.block);
        });
    }

    void print_expression(const Expression& expression)
    {
        std::visit(
            [this](const auto& node) {
                print_expression_node(node);
            },
            expression.node);
    }

    void print_expression_node(const IntegerLiteralExpression& expression)
    {
        write_indent();
        output_ << "IntegerLiteral value="
                << quote(expression.lexeme, '"') << '\n';
    }

    void print_expression_node(const BooleanLiteralExpression& expression)
    {
        write_indent();
        output_ << "BooleanLiteral value="
                << (expression.value ? "true" : "false") << '\n';
    }

    void print_expression_node(const CharacterLiteralExpression& expression)
    {
        write_indent();
        output_ << "CharacterLiteral value="
                << quote_byte(expression.value) << '\n';
    }

    void print_expression_node(const StringLiteralExpression& expression)
    {
        write_indent();
        output_ << "StringLiteral value="
                << quote(expression.value, '"') << '\n';
    }

    void print_expression_node(const IdentifierExpression& expression)
    {
        write_indent();
        output_ << "Identifier name=" << quote(expression.name, '"')
                << '\n';
    }

    void print_expression_node(const UnaryExpression& expression)
    {
        write_indent();
        output_ << "Unary operator="
                << quote(unary_operator_name(expression.operator_kind), '"')
                << '\n';
        indented([&] {
            role("Operand", [&] {
                print_expression(*expression.operand);
            });
        });
    }

    void print_expression_node(const BinaryExpression& expression)
    {
        write_indent();
        output_ << "Binary operator="
                << quote(binary_operator_name(expression.operator_kind), '"')
                << '\n';
        indented([&] {
            role("Left", [&] {
                print_expression(*expression.left);
            });
            role("Right", [&] {
                print_expression(*expression.right);
            });
        });
    }

    void print_expression_node(const CallExpression& expression)
    {
        line("Call");
        indented([&] {
            role("Callee", [&] {
                print_expression(*expression.callee);
            });
            line("Arguments");
            indented([&] {
                for (const auto& argument : expression.arguments) {
                    print_expression(*argument);
                }
            });
        });
    }

    void print_expression_node(const IndexExpression& expression)
    {
        line("Index");
        indented([&] {
            role("Base", [&] {
                print_expression(*expression.base);
            });
            role("Subscript", [&] {
                print_expression(*expression.index);
            });
        });
    }

    void print_expression_node(const MemberAccessExpression& expression)
    {
        write_indent();
        output_ << "MemberAccess member=" << quote(expression.member, '"')
                << '\n';
        indented([&] {
            role("Base", [&] {
                print_expression(*expression.base);
            });
        });
    }

    void print_expression_node(
        const VectorConstructionExpression& expression)
    {
        write_indent();
        output_ << "VectorConstruction type="
                << value_type_name(expression.type) << '\n';
        indented([&] {
            line("Arguments");
            indented([&] {
                for (const auto& argument : expression.arguments) {
                    print_expression(*argument);
                }
            });
        });
    }

    void print_expression_node(const ParenthesizedExpression& expression)
    {
        line("Parenthesized");
        indented([&] {
            role("Expression", [&] {
                print_expression(*expression.expression);
            });
        });
    }

    std::ostream& output_;
    std::size_t depth_ = 0;
};

}

void print_ast(std::ostream& output, const Program& program)
{
    AstPrinter{output}.print(program);
}

}

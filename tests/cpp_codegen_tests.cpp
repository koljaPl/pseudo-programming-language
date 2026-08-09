#include "test_support.hpp"

#include "pseudo/ast/program.hpp"
#include "pseudo/codegen/cpp_generator.hpp"
#include "pseudo/diagnostics/diagnostic_engine.hpp"
#include "pseudo/lexer/lexer.hpp"
#include "pseudo/parser/parser.hpp"
#include "pseudo/source/source_manager.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

class ParsedProgram {
public:
    explicit ParsedProgram(std::string source)
        : program_{tpp::SourceSpan{tpp::SourceId{0}, 0, 0}, {}}
    {
        const auto source_id = sources_.add_source(
            "codegen.tpp",
            std::move(source));
        tpp::Lexer lexer{source_id, sources_, frontend_diagnostics_};
        tokens_ = lexer.lex();
        TPP_CHECK(!frontend_diagnostics_.has_errors());

        tpp::Parser parser{tokens_, sources_, frontend_diagnostics_};
        program_ = parser.parse_program();
        TPP_CHECK(!frontend_diagnostics_.has_errors());
    }

    [[nodiscard]] tpp::Program& program() noexcept
    {
        return program_;
    }

    [[nodiscard]] const tpp::Program& program() const noexcept
    {
        return program_;
    }

    [[nodiscard]] const tpp::SourceManager& sources() const noexcept
    {
        return sources_;
    }

private:
    tpp::SourceManager sources_;
    tpp::DiagnosticEngine frontend_diagnostics_;
    std::vector<tpp::Token> tokens_;
    tpp::Program program_;
};

template <typename Node, typename Variant>
Node& require_variant(Variant& variant)
{
    auto* node = std::get_if<Node>(&variant);
    TPP_CHECK(node != nullptr);
    return *node;
}

tpp::FunctionDeclaration& require_main(tpp::Program& program)
{
    TPP_CHECK_EQ(program.declarations.size(), std::size_t{1});
    return require_variant<tpp::FunctionDeclaration>(
        program.declarations.front());
}

tpp::Statement& require_statement(tpp::BlockItem& item)
{
    return require_variant<tpp::Statement>(item);
}

tpp::Expression& require_expression_statement(tpp::BlockItem& item)
{
    auto& statement = require_statement(item);
    auto& expression_statement =
        require_variant<tpp::ExpressionStatement>(statement.node);
    TPP_CHECK(expression_statement.expression != nullptr);
    return *expression_statement.expression;
}

tpp::Expression& require_print_argument(tpp::BlockItem& item)
{
    auto& expression = require_expression_statement(item);
    auto& call = require_variant<tpp::CallExpression>(expression.node);
    TPP_CHECK_EQ(call.arguments.size(), std::size_t{1});
    TPP_CHECK(call.arguments.front() != nullptr);
    return *call.arguments.front();
}

std::string generate_source(const std::string_view source)
{
    const ParsedProgram parsed{std::string{source}};
    tpp::DiagnosticEngine diagnostics;
    const auto generated = tpp::generate_cpp(parsed.program(), diagnostics);

    TPP_CHECK(generated.has_value());
    TPP_CHECK(!diagnostics.has_errors());
    TPP_CHECK(diagnostics.diagnostics().empty());
    return *generated;
}

void empty_main_has_stable_output()
{
    const ParsedProgram parsed{"int main() {}"};
    constexpr std::string_view expected =
        "#include <cstdint>\n"
        "#include <iostream>\n"
        "#include <string>\n"
        "\n"
        "int main()\n"
        "{\n"
        "}\n";

    tpp::DiagnosticEngine first_diagnostics;
    const auto first =
        tpp::generate_cpp(parsed.program(), first_diagnostics);
    tpp::DiagnosticEngine second_diagnostics;
    const auto second =
        tpp::generate_cpp(parsed.program(), second_diagnostics);

    TPP_CHECK(first.has_value());
    TPP_CHECK(second.has_value());
    TPP_CHECK_EQ(*first, expected);
    TPP_CHECK_EQ(*second, expected);
    TPP_CHECK_EQ(*first, *second);
    TPP_CHECK_EQ(parsed.program().declarations.size(), std::size_t{1});
}

void print_supports_all_literals_and_escapes_bytes()
{
    constexpr std::string_view source = R"(
int main() {
    print(00042);
    print(true);
    print(false);
    print('\0');
    print('\'');
    print("A\0B\n\r\t\"\\'");
}
)";
    constexpr std::string_view expected = R"(#include <cstdint>
#include <iostream>
#include <string>

int main()
{
    std::cout << std::boolalpha << std::int64_t{42} << '\n';
    std::cout << std::boolalpha << true << '\n';
    std::cout << std::boolalpha << false << '\n';
    std::cout << std::boolalpha << '\000' << '\n';
    std::cout << std::boolalpha << '\'' << '\n';
    std::cout << std::boolalpha << std::string{"A\000B\012\015\011\"\\'", 9} << '\n';
}
)";

    TPP_CHECK_EQ(generate_source(source), expected);
}

void precedence_and_textual_operators_are_normalized()
{
    constexpr std::string_view symbolic =
        "int main() {"
        "print(1 + 2 * 3);"
        "print(!false || true && false);"
        "}";
    constexpr std::string_view textual =
        "int main() {"
        "print(1 + 2 * 3);"
        "print(not false or true and false);"
        "}";
    constexpr std::string_view expected =
        "#include <cstdint>\n"
        "#include <iostream>\n"
        "#include <string>\n"
        "\n"
        "int main()\n"
        "{\n"
        "    std::cout << std::boolalpha << "
        "(std::int64_t{1} + (std::int64_t{2} * std::int64_t{3}))"
        " << '\\n';\n"
        "    std::cout << std::boolalpha << "
        "((!false) || (true && false)) << '\\n';\n"
        "}\n";

    const auto symbolic_output = generate_source(symbolic);
    const auto textual_output = generate_source(textual);
    TPP_CHECK_EQ(symbolic_output, expected);
    TPP_CHECK_EQ(textual_output, expected);
    TPP_CHECK_EQ(symbolic_output, textual_output);
}

void every_operator_has_a_stable_cpp_spelling()
{
    constexpr std::string_view source =
        "int main() {"
        "print(+1); print(-1); print(!false);"
        "print(true || false); print(true && false);"
        "print(1 == 2); print(1 != 2);"
        "print(1 < 2); print(1 <= 2);"
        "print(1 > 2); print(1 >= 2);"
        "print(1 + 2); print(1 - 2);"
        "print(1 * 2); print(1 / 2); print(1 % 2);"
        "}";
    const auto output = generate_source(source);
    constexpr std::array<std::string_view, 16> expected_expressions{
        "(+std::int64_t{1})",
        "(-std::int64_t{1})",
        "(!false)",
        "(true || false)",
        "(true && false)",
        "(std::int64_t{1} == std::int64_t{2})",
        "(std::int64_t{1} != std::int64_t{2})",
        "(std::int64_t{1} < std::int64_t{2})",
        "(std::int64_t{1} <= std::int64_t{2})",
        "(std::int64_t{1} > std::int64_t{2})",
        "(std::int64_t{1} >= std::int64_t{2})",
        "(std::int64_t{1} + std::int64_t{2})",
        "(std::int64_t{1} - std::int64_t{2})",
        "(std::int64_t{1} * std::int64_t{2})",
        "(std::int64_t{1} / std::int64_t{2})",
        "(std::int64_t{1} % std::int64_t{2})",
    };

    for (const auto expression : expected_expressions) {
        tpp::test::check_contains(output, expression);
    }
}

void return_uses_the_main_abi_and_supports_signed_minimum()
{
    constexpr std::string_view arithmetic_source =
        "int main() { return 1 + 2 * 3; }";
    constexpr std::string_view arithmetic_line =
        "    return static_cast<int>((std::int64_t{1} + "
        "(std::int64_t{2} * std::int64_t{3})));\n";
    TPP_CHECK(
        generate_source(arithmetic_source).find(arithmetic_line)
        != std::string::npos);

    constexpr std::string_view minimum_source =
        "int main() { return -(09223372036854775808); }";
    constexpr std::string_view minimum_line =
        "    return static_cast<int>("
        "(-std::int64_t{9223372036854775807} - std::int64_t{1}));\n";
    TPP_CHECK(
        generate_source(minimum_source).find(minimum_line)
        != std::string::npos);
}

void arbitrary_non_printable_bytes_use_fixed_octal_escapes()
{
    ParsedProgram parsed{"int main() { print(\"x\"); }"};
    auto& argument = require_print_argument(
        require_main(parsed.program()).body->items.front());
    auto& literal =
        require_variant<tpp::StringLiteralExpression>(argument.node);
    literal.value = std::string{"\x01\xFF", 2};

    tpp::DiagnosticEngine diagnostics;
    const auto generated = tpp::generate_cpp(parsed.program(), diagnostics);

    TPP_CHECK(generated.has_value());
    TPP_CHECK(!diagnostics.has_errors());
    TPP_CHECK(
        generated->find("std::string{\"\\001\\377\", 2}")
        != std::string::npos);
}

void out_of_range_integer_reports_its_exact_span()
{
    constexpr std::string_view source =
        "int main() { print(9223372036854775809); }";
    const ParsedProgram parsed{std::string{source}};
    tpp::DiagnosticEngine diagnostics;

    const auto generated = tpp::generate_cpp(parsed.program(), diagnostics);

    TPP_CHECK(!generated.has_value());
    TPP_CHECK_EQ(diagnostics.error_count(), std::size_t{1});
    const auto reported = diagnostics.diagnostics();
    TPP_CHECK_EQ(
        reported.front().message,
        std::string{
            "integer literal is outside the supported signed 64-bit "
            "code-generation range"});
    TPP_CHECK(reported.front().primary_span.has_value());
    const auto span = *reported.front().primary_span;
    TPP_CHECK_EQ(
        parsed.sources().slice(span),
        std::string_view{"9223372036854775809"});
}

void signed_integer_boundaries_are_checked_without_conversion()
{
    const auto maximum = generate_source(
        "int main() { print(0009223372036854775807); }");
    tpp::test::check_contains(
        maximum,
        "std::int64_t{9223372036854775807}");

    constexpr std::array<std::string_view, 2> unsupported_sources{
        "int main() { print(+9223372036854775808); }",
        "int main() { print(-9223372036854775809); }",
    };

    for (const auto source : unsupported_sources) {
        const ParsedProgram parsed{std::string{source}};
        tpp::DiagnosticEngine diagnostics;
        const auto generated =
            tpp::generate_cpp(parsed.program(), diagnostics);

        TPP_CHECK(!generated.has_value());
        TPP_CHECK_EQ(diagnostics.error_count(), std::size_t{1});
        TPP_CHECK_EQ(
            diagnostics.diagnostics().front().message,
            std::string{
                "integer literal is outside the supported signed 64-bit "
                "code-generation range"});
    }
}

void malformed_integer_lexeme_is_diagnosed()
{
    ParsedProgram parsed{"int main() { print(1); }"};
    auto& argument = require_print_argument(
        require_main(parsed.program()).body->items.front());
    auto& literal =
        require_variant<tpp::IntegerLiteralExpression>(argument.node);
    literal.lexeme = "12x";
    tpp::DiagnosticEngine diagnostics;

    const auto generated = tpp::generate_cpp(parsed.program(), diagnostics);

    TPP_CHECK(!generated.has_value());
    TPP_CHECK_EQ(diagnostics.error_count(), std::size_t{1});
    TPP_CHECK_EQ(
        diagnostics.diagnostics().front().message,
        std::string{"malformed AST: invalid integer literal lexeme"});
}

void unsupported_program_shapes_report_without_partial_output()
{
    constexpr std::string_view source =
        "int global;"
        "void helper() {}"
        "int main() {}";
    const ParsedProgram parsed{std::string{source}};
    tpp::DiagnosticEngine diagnostics;

    const auto generated = tpp::generate_cpp(parsed.program(), diagnostics);

    TPP_CHECK(!generated.has_value());
    TPP_CHECK_EQ(diagnostics.error_count(), std::size_t{2});
    TPP_CHECK_EQ(
        diagnostics.diagnostics()[0].message,
        std::string{
            "C++ code generation does not support global variables yet"});
    TPP_CHECK_EQ(
        diagnostics.diagnostics()[1].message,
        std::string{
            "C++ code generation does not support top-level functions other "
            "than 'main' yet"});
}

void unsupported_main_body_constructs_are_independent_errors()
{
    constexpr std::string_view source =
        "int main() {"
        "int value;"
        "if true {}"
        "other();"
        "print();"
        "print(1, 2);"
        "print(value);"
        "return true;"
        "}";
    const ParsedProgram parsed{std::string{source}};
    tpp::DiagnosticEngine diagnostics;

    const auto generated = tpp::generate_cpp(parsed.program(), diagnostics);

    TPP_CHECK(!generated.has_value());
    TPP_CHECK_EQ(diagnostics.error_count(), std::size_t{7});
    const auto reported = diagnostics.diagnostics();
    TPP_CHECK_EQ(
        reported[0].message,
        std::string{
            "C++ code generation does not support local variables yet"});
    TPP_CHECK_EQ(
        reported[2].message,
        std::string{
            "C++ code generation only supports calls to builtin 'print'"});
    TPP_CHECK_EQ(
        reported[3].message,
        std::string{"builtin 'print' expects exactly one argument"});
    TPP_CHECK_EQ(
        reported[5].message,
        std::string{
            "C++ code generation does not support identifier expressions yet"});
    TPP_CHECK_EQ(
        reported[6].message,
        std::string{
            "C++ code generation only supports integer arithmetic in 'main' "
            "return statements"});
}

void unsupported_recursive_expression_nodes_have_specific_diagnostics()
{
    struct Case {
        std::string_view source;
        std::string_view message;
    };

    constexpr std::array<Case, 4> cases{
        Case{
            "int main() { print(other()); }",
            "C++ code generation does not support nested calls yet"},
        Case{
            "int main() { print(values[0]); }",
            "C++ code generation does not support indexing yet"},
        Case{
            "int main() { print(value.member); }",
            "C++ code generation does not support member access yet"},
        Case{
            "int main() { print(vector<int>(1)); }",
            "C++ code generation does not support vector construction yet"},
    };

    for (const auto& test_case : cases) {
        const ParsedProgram parsed{std::string{test_case.source}};
        tpp::DiagnosticEngine diagnostics;
        const auto generated =
            tpp::generate_cpp(parsed.program(), diagnostics);

        TPP_CHECK(!generated.has_value());
        TPP_CHECK_EQ(diagnostics.error_count(), std::size_t{1});
        TPP_CHECK_EQ(
            diagnostics.diagnostics().front().message,
            test_case.message);
    }
}

void unsupported_diagnostics_use_the_nearest_expression_spans()
{
    const ParsedProgram parsed{
        "int main() { print(value); print(1, 2); }"};
    tpp::DiagnosticEngine diagnostics;

    const auto generated = tpp::generate_cpp(parsed.program(), diagnostics);

    TPP_CHECK(!generated.has_value());
    TPP_CHECK_EQ(diagnostics.error_count(), std::size_t{2});
    const auto reported = diagnostics.diagnostics();
    TPP_CHECK(reported[0].primary_span.has_value());
    TPP_CHECK(reported[1].primary_span.has_value());
    TPP_CHECK_EQ(
        parsed.sources().slice(*reported[0].primary_span),
        std::string_view{"value"});
    TPP_CHECK_EQ(
        parsed.sources().slice(*reported[1].primary_span),
        std::string_view{"print(1, 2)"});
}

void missing_and_invalid_main_are_diagnosed()
{
    const ParsedProgram empty{""};
    tpp::DiagnosticEngine empty_diagnostics;
    TPP_CHECK(!tpp::generate_cpp(
        empty.program(),
        empty_diagnostics).has_value());
    TPP_CHECK_EQ(empty_diagnostics.error_count(), std::size_t{1});
    TPP_CHECK_EQ(
        empty_diagnostics.diagnostics().front().message,
        std::string{
            "C++ code generation requires a top-level 'int main()' function"});
    TPP_CHECK(
        empty_diagnostics.diagnostics().front().primary_span.has_value());
    const auto empty_span =
        *empty_diagnostics.diagnostics().front().primary_span;
    TPP_CHECK_EQ(empty_span.begin, std::size_t{0});
    TPP_CHECK_EQ(empty_span.end, std::size_t{0});

    const ParsedProgram invalid{"void main(int argument) {}"};
    tpp::DiagnosticEngine invalid_diagnostics;
    TPP_CHECK(!tpp::generate_cpp(
        invalid.program(),
        invalid_diagnostics).has_value());
    TPP_CHECK_EQ(invalid_diagnostics.error_count(), std::size_t{1});
    const auto diagnostic = invalid_diagnostics.diagnostics().front();
    TPP_CHECK(diagnostic.primary_span.has_value());
    TPP_CHECK_EQ(
        invalid.sources().slice(*diagnostic.primary_span),
        std::string_view{"main"});

    const ParsedProgram helper_only{"void helper() {}"};
    tpp::DiagnosticEngine helper_diagnostics;
    TPP_CHECK(!tpp::generate_cpp(
        helper_only.program(),
        helper_diagnostics).has_value());
    TPP_CHECK_EQ(helper_diagnostics.error_count(), std::size_t{2});
    TPP_CHECK_EQ(
        helper_diagnostics.diagnostics()[1].message,
        std::string{
            "C++ code generation requires a top-level 'int main()' function"});
}

void duplicate_main_is_rejected()
{
    const ParsedProgram parsed{"int main() {} int main() {}"};
    tpp::DiagnosticEngine diagnostics;

    const auto generated = tpp::generate_cpp(parsed.program(), diagnostics);

    TPP_CHECK(!generated.has_value());
    TPP_CHECK_EQ(diagnostics.error_count(), std::size_t{1});
    TPP_CHECK_EQ(
        diagnostics.diagnostics().front().message,
        std::string{
            "C++ code generation requires exactly one top-level 'main' "
            "function"});
}

void malformed_recursive_ast_reports_instead_of_crashing()
{
    ParsedProgram parsed{"int main() { print(-1); }"};
    auto& argument = require_print_argument(
        require_main(parsed.program()).body->items.front());
    const auto argument_span = argument.span;
    auto& unary = require_variant<tpp::UnaryExpression>(argument.node);
    unary.operand.reset();

    tpp::DiagnosticEngine diagnostics;
    const auto generated = tpp::generate_cpp(parsed.program(), diagnostics);

    TPP_CHECK(!generated.has_value());
    TPP_CHECK_EQ(diagnostics.error_count(), std::size_t{1});
    TPP_CHECK_EQ(
        diagnostics.diagnostics().front().message,
        std::string{"malformed AST: unary expression has no operand"});
    TPP_CHECK(diagnostics.diagnostics().front().primary_span.has_value());
    const auto diagnostic_span =
        *diagnostics.diagnostics().front().primary_span;
    TPP_CHECK_EQ(diagnostic_span.source.value, argument_span.source.value);
    TPP_CHECK_EQ(diagnostic_span.begin, argument_span.begin);
    TPP_CHECK_EQ(diagnostic_span.end, argument_span.end);
}

void other_missing_recursive_children_are_diagnosed()
{
    {
        ParsedProgram parsed{"int main() { print(1 + 2); }"};
        auto& argument = require_print_argument(
            require_main(parsed.program()).body->items.front());
        auto& binary =
            require_variant<tpp::BinaryExpression>(argument.node);
        binary.left.reset();
        tpp::DiagnosticEngine diagnostics;

        TPP_CHECK(!tpp::generate_cpp(
            parsed.program(),
            diagnostics).has_value());
        TPP_CHECK_EQ(diagnostics.error_count(), std::size_t{1});
        TPP_CHECK_EQ(
            diagnostics.diagnostics().front().message,
            std::string{
                "malformed AST: binary expression is missing an operand"});
    }

    {
        ParsedProgram parsed{"int main() { print((1)); }"};
        auto& argument = require_print_argument(
            require_main(parsed.program()).body->items.front());
        auto& parenthesized =
            require_variant<tpp::ParenthesizedExpression>(argument.node);
        parenthesized.expression.reset();
        tpp::DiagnosticEngine diagnostics;

        TPP_CHECK(!tpp::generate_cpp(
            parsed.program(),
            diagnostics).has_value());
        TPP_CHECK_EQ(diagnostics.error_count(), std::size_t{1});
        TPP_CHECK_EQ(
            diagnostics.diagnostics().front().message,
            std::string{
                "malformed AST: parenthesized expression has no expression"});
    }
}

void valueless_return_is_rejected()
{
    const ParsedProgram parsed{"int main() { return; }"};
    tpp::DiagnosticEngine diagnostics;

    const auto generated = tpp::generate_cpp(parsed.program(), diagnostics);

    TPP_CHECK(!generated.has_value());
    TPP_CHECK_EQ(diagnostics.error_count(), std::size_t{1});
    TPP_CHECK_EQ(
        diagnostics.diagnostics().front().message,
        std::string{
            "C++ code generation requires a value in a 'main' return "
            "statement"});
}

void malformed_main_body_reports_instead_of_crashing()
{
    ParsedProgram parsed{"int main() {}"};
    require_main(parsed.program()).body.reset();
    tpp::DiagnosticEngine diagnostics;

    const auto generated = tpp::generate_cpp(parsed.program(), diagnostics);

    TPP_CHECK(!generated.has_value());
    TPP_CHECK_EQ(diagnostics.error_count(), std::size_t{1});
    TPP_CHECK_EQ(
        diagnostics.diagnostics().front().message,
        std::string{"malformed AST: 'main' function has no body"});
}

}

int main()
{
    return tpp::test::run({
        {"empty main has stable output", empty_main_has_stable_output},
        {"print supports literals and escapes",
         print_supports_all_literals_and_escapes_bytes},
        {"precedence and textual operators are normalized",
         precedence_and_textual_operators_are_normalized},
        {"all operator spellings", every_operator_has_a_stable_cpp_spelling},
        {"return ABI and signed minimum",
         return_uses_the_main_abi_and_supports_signed_minimum},
        {"arbitrary bytes use octal escapes",
         arbitrary_non_printable_bytes_use_fixed_octal_escapes},
        {"out-of-range integer span",
         out_of_range_integer_reports_its_exact_span},
        {"signed integer boundaries",
         signed_integer_boundaries_are_checked_without_conversion},
        {"malformed integer lexeme", malformed_integer_lexeme_is_diagnosed},
        {"unsupported program shapes",
         unsupported_program_shapes_report_without_partial_output},
        {"independent unsupported body errors",
         unsupported_main_body_constructs_are_independent_errors},
        {"unsupported recursive expressions",
         unsupported_recursive_expression_nodes_have_specific_diagnostics},
        {"unsupported diagnostic spans",
         unsupported_diagnostics_use_the_nearest_expression_spans},
        {"missing and invalid main", missing_and_invalid_main_are_diagnosed},
        {"duplicate main", duplicate_main_is_rejected},
        {"malformed recursive AST",
         malformed_recursive_ast_reports_instead_of_crashing},
        {"other missing recursive children",
         other_missing_recursive_children_are_diagnosed},
        {"valueless return", valueless_return_is_rejected},
        {"malformed main body",
         malformed_main_body_reports_instead_of_crashing},
    });
}

#include "test_support.hpp"

#include "pseudo/ast/program.hpp"
#include "pseudo/codegen/cpp_generator.hpp"
#include "pseudo/diagnostics/diagnostic_engine.hpp"
#include "pseudo/lexer/lexer.hpp"
#include "pseudo/parser/parser.hpp"
#include "pseudo/semantic/control_flow_checker.hpp"
#include "pseudo/semantic/declaration_collector.hpp"
#include "pseudo/semantic/declaration_info.hpp"
#include "pseudo/semantic/name_resolver.hpp"
#include "pseudo/semantic/resolution_info.hpp"
#include "pseudo/semantic/symbol_table.hpp"
#include "pseudo/semantic/type_checker.hpp"
#include "pseudo/semantic/type_context.hpp"
#include "pseudo/semantic/type_info.hpp"
#include "pseudo/source/source_manager.hpp"

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

class CheckedProgram {
public:
    explicit CheckedProgram(std::string source)
        : program_{tpp::SourceSpan{tpp::SourceId{0}, 0, 0}, {}}
    {
        const auto source_id = sources_.add_source(
            "codegen.tpp",
            std::move(source));

        tpp::Lexer lexer{source_id, sources_, semantic_diagnostics_};
        tokens_ = lexer.lex();
        TPP_CHECK(!semantic_diagnostics_.has_errors());

        tpp::Parser parser{tokens_, sources_, semantic_diagnostics_};
        program_ = parser.parse_program();
        TPP_CHECK(!semantic_diagnostics_.has_errors());

        tpp::DeclarationCollector collector{
            types_,
            symbols_,
            declarations_,
            semantic_diagnostics_,
        };
        TPP_CHECK(collector.collect(program_));

        tpp::NameResolver resolver{
            symbols_,
            declarations_,
            resolutions_,
            semantic_diagnostics_,
        };
        TPP_CHECK(resolver.resolve(program_));

        tpp::TypeChecker type_checker{
            types_,
            symbols_,
            declarations_,
            resolutions_,
            type_info_,
            semantic_diagnostics_,
        };
        TPP_CHECK(type_checker.check(program_));

        tpp::ControlFlowChecker control_flow_checker{
            semantic_diagnostics_};
        TPP_CHECK(control_flow_checker.check(program_));
        TPP_CHECK(!semantic_diagnostics_.has_errors());
    }

    [[nodiscard]] tpp::CppGenerationContext context() const noexcept
    {
        return tpp::CppGenerationContext{
            .types = types_,
            .symbols = symbols_,
            .declarations = declarations_,
            .resolutions = resolutions_,
            .type_info = type_info_,
        };
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

    [[nodiscard]] const tpp::TypeContext& types() const noexcept
    {
        return types_;
    }

    [[nodiscard]] const tpp::SymbolTable& symbols() const noexcept
    {
        return symbols_;
    }

    [[nodiscard]] const tpp::DeclarationInfo& declarations() const noexcept
    {
        return declarations_;
    }

    [[nodiscard]] const tpp::ResolutionInfo& resolutions() const noexcept
    {
        return resolutions_;
    }

    [[nodiscard]] const tpp::TypeInfo& type_info() const noexcept
    {
        return type_info_;
    }

private:
    tpp::SourceManager sources_;
    tpp::DiagnosticEngine semantic_diagnostics_;
    std::vector<tpp::Token> tokens_;
    tpp::Program program_;
    tpp::TypeContext types_;
    tpp::SymbolTable symbols_;
    tpp::DeclarationInfo declarations_;
    tpp::ResolutionInfo resolutions_;
    tpp::TypeInfo type_info_;
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
    for (auto& declaration : program.declarations) {
        auto* function = std::get_if<tpp::FunctionDeclaration>(&declaration);
        if (function != nullptr && function->name == "main") {
            return *function;
        }
    }

    TPP_CHECK(false);
    return require_variant<tpp::FunctionDeclaration>(
        program.declarations.front());
}

tpp::FunctionDeclaration& require_function(
    tpp::Program& program,
    const std::string_view name)
{
    for (auto& declaration : program.declarations) {
        auto* function = std::get_if<tpp::FunctionDeclaration>(&declaration);
        if (function != nullptr && function->name == name) {
            return *function;
        }
    }

    TPP_CHECK(false);
    return require_variant<tpp::FunctionDeclaration>(
        program.declarations.front());
}

tpp::Statement& require_statement(tpp::BlockItem& item)
{
    return require_variant<tpp::Statement>(item);
}

template <typename Node>
Node& require_statement_node(tpp::Block& block, const std::size_t index)
{
    TPP_CHECK(index < block.items.size());
    return require_variant<Node>(require_statement(block.items[index]).node);
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

std::optional<std::string> generate(
    const CheckedProgram& checked,
    tpp::DiagnosticEngine& diagnostics)
{
    const auto context = checked.context();
    return tpp::generate_cpp(checked.program(), context, diagnostics);
}

std::string generate_source(const std::string_view source)
{
    const CheckedProgram checked{std::string{source}};
    tpp::DiagnosticEngine diagnostics;
    const auto generated = generate(checked, diagnostics);

    TPP_CHECK(generated.has_value());
    TPP_CHECK(!diagnostics.has_errors());
    TPP_CHECK(diagnostics.diagnostics().empty());
    return *generated;
}

void check_has_diagnostic(
    const tpp::DiagnosticEngine& diagnostics,
    const std::string_view text)
{
    std::string messages;
    for (const auto& diagnostic : diagnostics.diagnostics()) {
        if (diagnostic.message.find(text) != std::string::npos) {
            return;
        }
        if (!messages.empty()) {
            messages += '\n';
        }
        messages += diagnostic.message;
    }

    throw tpp::test::Failure{
        "expected diagnostics to contain '" + std::string{text}
        + "', got:\n" + messages};
}

std::size_t count_occurrences(
    const std::string_view text,
    const std::string_view needle)
{
    TPP_CHECK(!needle.empty());
    std::size_t count = 0;
    std::size_t position = 0;
    while ((position = text.find(needle, position)) != std::string_view::npos) {
        ++count;
        position += needle.size();
    }
    return count;
}

void empty_main_has_stable_output()
{
    const CheckedProgram checked{"int main() {}"};
    constexpr std::string_view expected =
        "#include <cstdint>\n"
        "#include <iostream>\n"
        "#include <string>\n"
        "\n"
        "int main()\n"
        "{\n"
        "}\n";

    tpp::DiagnosticEngine first_diagnostics;
    const auto first = generate(checked, first_diagnostics);
    tpp::DiagnosticEngine second_diagnostics;
    const auto second = generate(checked, second_diagnostics);

    TPP_CHECK(first.has_value());
    TPP_CHECK(second.has_value());
    TPP_CHECK_EQ(*first, expected);
    TPP_CHECK_EQ(*second, expected);
    TPP_CHECK_EQ(*first, *second);
    TPP_CHECK_EQ(checked.program().declarations.size(), std::size_t{1});
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
    const auto arithmetic = generate_source(
        "int identity(int value) { return value; }"
        "int main() { return identity(1 + 2 * 3); }");
    tpp::test::check_contains(
        arithmetic,
        "return static_cast<int>(tpp_function_0((std::int64_t{1} + "
        "(std::int64_t{2} * std::int64_t{3}))));");

    const auto minimum = generate_source(
        "int main() { return -(09223372036854775808); }");
    tpp::test::check_contains(
        minimum,
        "return static_cast<int>((-std::int64_t{9223372036854775807} "
        "- std::int64_t{1}));");
}

void arbitrary_non_printable_bytes_use_fixed_octal_escapes()
{
    CheckedProgram checked{"int main() { print(\"x\"); }"};
    auto& argument = require_print_argument(
        require_main(checked.program()).body->items.front());
    auto& literal =
        require_variant<tpp::StringLiteralExpression>(argument.node);
    literal.value = std::string{"\x01\xFF", 2};

    tpp::DiagnosticEngine diagnostics;
    const auto generated = generate(checked, diagnostics);

    TPP_CHECK(generated.has_value());
    TPP_CHECK(!diagnostics.has_errors());
    tpp::test::check_contains(
        *generated,
        "std::string{\"\\001\\377\", 2}");
}

void integer_codegen_defensively_validates_mutated_lexemes()
{
    {
        CheckedProgram checked{"int main() { print(0000000000000000001); }"};
        auto& argument = require_print_argument(
            require_main(checked.program()).body->items.front());
        auto& literal =
            require_variant<tpp::IntegerLiteralExpression>(argument.node);
        literal.lexeme = "9223372036854775809";
        const auto expected_span = argument.span;
        tpp::DiagnosticEngine diagnostics;

        const auto generated = generate(checked, diagnostics);

        TPP_CHECK(!generated.has_value());
        TPP_CHECK_EQ(diagnostics.error_count(), std::size_t{1});
        TPP_CHECK_EQ(
            diagnostics.diagnostics().front().message,
            std::string{
                "integer literal is outside the supported signed 64-bit "
                "code-generation range"});
        TPP_CHECK(diagnostics.diagnostics().front().primary_span.has_value());
        const auto actual_span =
            *diagnostics.diagnostics().front().primary_span;
        TPP_CHECK_EQ(actual_span.source, expected_span.source);
        TPP_CHECK_EQ(actual_span.begin, expected_span.begin);
        TPP_CHECK_EQ(actual_span.end, expected_span.end);
    }

    {
        CheckedProgram checked{"int main() { print(1); }"};
        auto& argument = require_print_argument(
            require_main(checked.program()).body->items.front());
        auto& literal =
            require_variant<tpp::IntegerLiteralExpression>(argument.node);
        literal.lexeme = "12x";
        tpp::DiagnosticEngine diagnostics;

        TPP_CHECK(!generate(checked, diagnostics).has_value());
        TPP_CHECK_EQ(diagnostics.error_count(), std::size_t{1});
        TPP_CHECK_EQ(
            diagnostics.diagnostics().front().message,
            std::string{"malformed AST: invalid integer literal lexeme"});
    }
}

void top_level_functions_have_stable_prototypes_and_definitions()
{
    constexpr std::string_view source = R"(int add(int a, int b) {
    return a + b;
}

void greet(string name) {
    print(name);
}

int main() {
    greet("sum");
    print(add(2, 3));
    return add(0, 0);
}
)";
    constexpr std::string_view expected = R"(#include <cstdint>
#include <iostream>
#include <string>

std::int64_t tpp_function_0(std::int64_t tpp_parameter_1, std::int64_t tpp_parameter_2);
void tpp_function_3(std::string tpp_parameter_4);

std::int64_t tpp_function_0(std::int64_t tpp_parameter_1, std::int64_t tpp_parameter_2)
{
    return (tpp_parameter_1 + tpp_parameter_2);
}

void tpp_function_3(std::string tpp_parameter_4)
{
    std::cout << std::boolalpha << tpp_parameter_4 << '\n';
}

int main()
{
    tpp_function_3(std::string{"sum", 3});
    std::cout << std::boolalpha << tpp_function_0(std::int64_t{2}, std::int64_t{3}) << '\n';
    return static_cast<int>(tpp_function_0(std::int64_t{0}, std::int64_t{0}));
}
)";

    TPP_CHECK_EQ(generate_source(source), expected);
}

void all_scalar_parameter_and_return_types_are_supported()
{
    constexpr std::string_view source = R"(int identity_int(int value) {
    return value;
}
bool identity_bool(bool value) {
    return value;
}
char identity_char(char value) {
    return value;
}
string identity_string(string value) {
    return value;
}
void consume(string value) {
    print(value);
    return;
}
int main() {
    print(identity_int(1));
    print(identity_bool(true));
    print(identity_char('x'));
    consume(identity_string("ok"));
    return 0;
}
)";

    const auto output = generate_source(source);
    tpp::test::check_contains(
        output,
        "std::int64_t tpp_function_0(std::int64_t tpp_parameter_1);");
    tpp::test::check_contains(
        output,
        "bool tpp_function_2(bool tpp_parameter_3);");
    tpp::test::check_contains(
        output,
        "char tpp_function_4(char tpp_parameter_5);");
    tpp::test::check_contains(
        output,
        "std::string tpp_function_6(std::string tpp_parameter_7);");
    tpp::test::check_contains(
        output,
        "void tpp_function_8(std::string tpp_parameter_9);");
    tpp::test::check_contains(output, "int main()");
    tpp::test::check_contains(output, "return tpp_parameter_7;");
    tpp::test::check_contains(output, "return;\n");
}

void forward_calls_recursion_and_mutual_recursion_use_prototypes()
{
    constexpr std::string_view source = R"(int first(int value) {
    return second(value);
}
int second(int value) {
    return first(value);
}
int recurse(int value) {
    return recurse(value);
}
int main() {
    return first(0);
}
)";

    const auto output = generate_source(source);
    const auto first_prototype = output.find(
        "std::int64_t tpp_function_0(std::int64_t tpp_parameter_1);");
    const auto second_prototype = output.find(
        "std::int64_t tpp_function_2(std::int64_t tpp_parameter_3);");
    const auto recurse_prototype = output.find(
        "std::int64_t tpp_function_4(std::int64_t tpp_parameter_5);");
    const auto first_definition = output.find(
        "std::int64_t tpp_function_0(std::int64_t tpp_parameter_1)\n{");

    TPP_CHECK(first_prototype != std::string::npos);
    TPP_CHECK(second_prototype != std::string::npos);
    TPP_CHECK(recurse_prototype != std::string::npos);
    TPP_CHECK(first_definition != std::string::npos);
    TPP_CHECK(first_prototype < first_definition);
    TPP_CHECK(second_prototype < first_definition);
    TPP_CHECK(recurse_prototype < first_definition);
    tpp::test::check_contains(
        output,
        "return tpp_function_2(tpp_parameter_1);");
    tpp::test::check_contains(
        output,
        "return tpp_function_0(tpp_parameter_3);");
    tpp::test::check_contains(
        output,
        "return tpp_function_4(tpp_parameter_5);");
}

void calls_work_in_statements_returns_print_and_nested_arguments()
{
    constexpr std::string_view source = R"(int identity(int value) {
    return value;
}
void consume(int value) {
    identity(value);
    print(identity(value));
}
int main() {
    consume(identity(1));
    print(identity(identity(2)));
    return identity(0);
}
)";

    const auto output = generate_source(source);
    tpp::test::check_contains(
        output,
        "    tpp_function_0(tpp_parameter_3);\n");
    tpp::test::check_contains(
        output,
        "std::cout << std::boolalpha << "
        "tpp_function_0(tpp_parameter_3) << '\\n';");
    tpp::test::check_contains(
        output,
        "tpp_function_2(tpp_function_0(std::int64_t{1}));");
    tpp::test::check_contains(
        output,
        "tpp_function_0(tpp_function_0(std::int64_t{2}))");
    tpp::test::check_contains(
        output,
        "return static_cast<int>(tpp_function_0(std::int64_t{0}));");
}

void parenthesized_parameters_and_callees_preserve_structure()
{
    constexpr std::string_view source = R"(int identity(int value) {
    return (value);
}
int main() {
    print((identity)(1));
    return (identity)(0);
}
)";

    const auto output = generate_source(source);
    tpp::test::check_contains(output, "return (tpp_parameter_1);");
    tpp::test::check_contains(
        output,
        "std::cout << std::boolalpha << "
        "tpp_function_0(std::int64_t{1}) << '\\n';");
    tpp::test::check_contains(
        output,
        "return static_cast<int>(tpp_function_0(std::int64_t{0}));");
}

void generated_names_do_not_copy_cpp_keywords()
{
    constexpr std::string_view source = R"(int class(int template) {
    return template;
}
int operator(int namespace) {
    return class(namespace);
}
int main() {
    return operator(0);
}
)";

    const auto output = generate_source(source);
    TPP_CHECK(output.find("class") == std::string::npos);
    TPP_CHECK(output.find("template") == std::string::npos);
    TPP_CHECK(output.find("operator") == std::string::npos);
    TPP_CHECK(output.find("namespace") == std::string::npos);
    tpp::test::check_contains(
        output,
        "std::int64_t tpp_function_0(std::int64_t tpp_parameter_1);");
    tpp::test::check_contains(
        output,
        "return tpp_function_0(tpp_parameter_3);");
}

void user_function_named_print_shadows_the_builtin()
{
    constexpr std::string_view source = R"(int print(int value) {
    return value;
}
int main() {
    return print(1);
}
)";

    const auto output = generate_source(source);
    tpp::test::check_contains(
        output,
        "return static_cast<int>(tpp_function_0(std::int64_t{1}));");
    TPP_CHECK(output.find("std::cout") == std::string::npos);
}

void string_operations_use_semantic_runtime_helpers()
{
    constexpr std::string_view source = R"(string edit(
    string text,
    char replacement,
    int index
) {
    text[index] = replacement;
    text += "!";
    text.push(replacement);
    return text;
}

void inspect(string text) {
    print(text[0]);
    print(len(text));
    print(substring(text, 0, text.length()));
    print(text + "x");
    print(text == "x");
    print(text != "x");
    print(text < "x");
    print(text <= "x");
    print(text > "x");
    print(text >= "x");
}

int main() {
    inspect(edit("abc", 'z', 1));
    return 0;
}
)";

    const auto output = generate_source(source);
    tpp::test::check_contains(output, "#include <pseudo/runtime.hpp>\n");
    tpp::test::check_contains(
        output,
        "tpp::runtime::string_index(tpp_parameter_1, tpp_parameter_3) "
        "= tpp_parameter_2;");
    tpp::test::check_contains(
        output,
        "tpp_parameter_1 += std::string{\"!\", 1};");
    tpp::test::check_contains(
        output,
        "tpp::runtime::string_push(tpp_parameter_1, tpp_parameter_2);");
    tpp::test::check_contains(
        output,
        "tpp::runtime::string_index(tpp_parameter_5, std::int64_t{0})");
    tpp::test::check_contains(
        output,
        "tpp::runtime::string_length(tpp_parameter_5)");
    tpp::test::check_contains(
        output,
        "tpp::runtime::substring(tpp_parameter_5, std::int64_t{0}, "
        "tpp::runtime::string_length(tpp_parameter_5))");
    for (const auto spelling : {
             " + std::string{\"x\", 1})",
             " == std::string{\"x\", 1})",
             " != std::string{\"x\", 1})",
             " < std::string{\"x\", 1})",
             " <= std::string{\"x\", 1})",
             " > std::string{\"x\", 1})",
             " >= std::string{\"x\", 1})",
         }) {
        tpp::test::check_contains(output, spelling);
    }
}

void initialized_string_and_char_locals_are_supported()
{
    constexpr std::string_view source = R"(string build() {
    string text = "ab";
    char replacement = 'z';
    replacement = 'y';
    text = text + "!";
    text[0] = replacement;
    return text;
}

int main() {
    print(build());
    return 0;
}
)";

    const auto output = generate_source(source);
    tpp::test::check_contains(
        output,
        "std::string tpp_variable_1 = std::string{\"ab\", 2};");
    tpp::test::check_contains(output, "char tpp_variable_2 = 'z';");
    tpp::test::check_contains(output, "tpp_variable_2 = 'y';");
    tpp::test::check_contains(
        output,
        "tpp_variable_1 = (tpp_variable_1 + std::string{\"!\", 1});");
    tpp::test::check_contains(
        output,
        "tpp::runtime::string_index(tpp_variable_1, std::int64_t{0}) "
        "= tpp_variable_2;");
    tpp::test::check_contains(output, "return tpp_variable_1;");
}

void temporary_strings_support_indexing_and_length()
{
    constexpr std::string_view source = R"(string identity(string value) {
    return value;
}
int main() {
    print("abc"[1]);
    print(identity("xyz")[2]);
    print(("a" + "b").length());
    print(identity("abcd").length());
    return 0;
}
)";

    const auto output = generate_source(source);
    tpp::test::check_contains(
        output,
        "tpp::runtime::string_index(std::string{\"abc\", 3}, "
        "std::int64_t{1})");
    tpp::test::check_contains(
        output,
        "tpp::runtime::string_index(tpp_function_0("
        "std::string{\"xyz\", 3}), std::int64_t{2})");
    tpp::test::check_contains(
        output,
        "tpp::runtime::string_length(((std::string{\"a\", 1} + "
        "std::string{\"b\", 1})))");
    tpp::test::check_contains(
        output,
        "tpp::runtime::string_length(tpp_function_0("
        "std::string{\"abcd\", 4}))");
}

void vector_types_construction_and_storage_are_recursive()
{
    constexpr std::string_view source = R"(vector<int> keep_int(vector<int> value) {
    return value;
}
vector<bool> keep_bool(vector<bool> value) {
    return value;
}
vector<char> keep_char(vector<char> value) {
    return value;
}
vector<string> keep_string(vector<string> value) {
    return value;
}
vector<vector<string>> keep_nested(vector<vector<string>> value) {
    return value;
}
int main() {
    vector<int> integers = vector<int>();
    vector<bool> booleans = vector<bool>(2);
    vector<char> characters = vector<char>(2, 'x');
    vector<string> strings = vector<string>(1, "item");
    vector<vector<string>> nested =
        vector<vector<string>>(2, vector<string>(1, "nested"));
    integers = keep_int(integers);
    booleans = keep_bool(booleans);
    characters = keep_char(characters);
    strings = keep_string(strings);
    nested = keep_nested(nested);
    return 0;
}
)";

    const auto output = generate_source(source);
    tpp::test::check_contains(output, "#include <vector>\n");
    tpp::test::check_contains(output, "#include <pseudo/runtime.hpp>\n");
    tpp::test::check_contains(output, "std::vector<std::int64_t>");
    tpp::test::check_contains(output, "std::vector<bool>");
    tpp::test::check_contains(output, "std::vector<char>");
    tpp::test::check_contains(output, "std::vector<std::string>");
    tpp::test::check_contains(
        output,
        "std::vector<std::vector<std::string>>");
    tpp::test::check_contains(
        output,
        "std::vector<std::int64_t>{}");
    tpp::test::check_contains(
        output,
        "tpp::runtime::make_vector<bool>(std::int64_t{2})");
    tpp::test::check_contains(
        output,
        "tpp::runtime::make_vector<char>(std::int64_t{2}, 'x')");
    tpp::test::check_contains(
        output,
        "tpp::runtime::make_vector<std::string>(std::int64_t{1}, "
        "std::string{\"item\", 4})");
    tpp::test::check_contains(
        output,
        "tpp::runtime::make_vector<std::vector<std::string>>("
        "std::int64_t{2}, tpp::runtime::make_vector<std::string>("
        "std::int64_t{1}, std::string{\"nested\", 6}))");
}

void vector_indexing_and_assignments_use_checked_runtime()
{
    constexpr std::string_view source = R"(int inspect(
    vector<vector<int>> matrix,
    vector<bool> flags,
    vector<string> texts
) {
    matrix[0][1] = 4;
    matrix[1][1] += 2;
    flags[0] = true;
    texts[0] = "ok";
    texts[1][0] = 'X';
    print(matrix[0][1]);
    print(flags[0]);
    print(texts[1][0]);
    return matrix[1][1];
}
int main() {
    vector<vector<int>> matrix =
        vector<vector<int>>(2, vector<int>(2, 0));
    vector<bool> flags = vector<bool>(1, false);
    vector<string> texts = vector<string>(2, "hi");
    return inspect(matrix, flags, texts);
}
)";

    const auto output = generate_source(source);
    tpp::test::check_contains(
        output,
        "tpp::runtime::vector_index(tpp::runtime::vector_index("
        "tpp_parameter_1, std::int64_t{0}), std::int64_t{1}) "
        "= std::int64_t{4};");
    tpp::test::check_contains(
        output,
        "tpp::runtime::vector_index(tpp::runtime::vector_index("
        "tpp_parameter_1, std::int64_t{1}), std::int64_t{1}) "
        "+= std::int64_t{2};");
    tpp::test::check_contains(
        output,
        "tpp::runtime::vector_index(tpp_parameter_2, std::int64_t{0}) "
        "= true;");
    tpp::test::check_contains(
        output,
        "tpp::runtime::vector_index(tpp_parameter_3, std::int64_t{0}) "
        "= std::string{\"ok\", 2};");
    tpp::test::check_contains(
        output,
        "tpp::runtime::string_index(tpp::runtime::vector_index("
        "tpp_parameter_3, std::int64_t{1}), std::int64_t{0}) = 'X';");
}

void malformed_vector_state_has_no_partial_output()
{
    {
        CheckedProgram checked{R"(int main() {
    vector<int> values = vector<int>(1, 0);
    return 0;
}
)"};
        auto& body = *require_main(checked.program()).body;
        auto& declaration = require_variant<tpp::VariableDeclaration>(
            require_statement(body.items.front()).node);
        TPP_CHECK(declaration.initializer != nullptr);
        auto& construction =
            require_variant<tpp::VectorConstructionExpression>(
                declaration.initializer->node);
        auto& vector_type =
            require_variant<tpp::VectorType>(construction.type.node);
        TPP_CHECK(vector_type.element_type != nullptr);
        vector_type.element_type->node = tpp::ScalarTypeKind::string;
        tpp::DiagnosticEngine diagnostics;

        TPP_CHECK(!generate(checked, diagnostics).has_value());
        TPP_CHECK(diagnostics.has_errors());
        check_has_diagnostic(diagnostics, "semantic type");
    }

    {
        CheckedProgram checked{R"(int main() {
    vector<int> values = vector<int>(1, 0);
    return 0;
}
)"};
        auto& body = *require_main(checked.program()).body;
        auto& declaration = require_variant<tpp::VariableDeclaration>(
            require_statement(body.items.front()).node);
        TPP_CHECK(declaration.initializer != nullptr);
        auto& construction =
            require_variant<tpp::VectorConstructionExpression>(
                declaration.initializer->node);
        TPP_CHECK_EQ(construction.arguments.size(), std::size_t{2});
        construction.arguments.front().reset();
        tpp::DiagnosticEngine diagnostics;

        TPP_CHECK(!generate(checked, diagnostics).has_value());
        TPP_CHECK(diagnostics.has_errors());
        check_has_diagnostic(diagnostics, "malformed AST");
    }

    {
        CheckedProgram checked{R"(int main() {
    vector<string> values = vector<string>(1, "item");
    return 0;
}
)"};
        auto& body = *require_main(checked.program()).body;
        auto& declaration = require_variant<tpp::VariableDeclaration>(
            require_statement(body.items.front()).node);
        TPP_CHECK(declaration.initializer != nullptr);
        auto& construction =
            require_variant<tpp::VectorConstructionExpression>(
                declaration.initializer->node);
        TPP_CHECK_EQ(construction.arguments.size(), std::size_t{2});
        std::swap(construction.arguments[0], construction.arguments[1]);
        tpp::DiagnosticEngine diagnostics;

        TPP_CHECK(!generate(checked, diagnostics).has_value());
        TPP_CHECK(diagnostics.has_errors());
        check_has_diagnostic(diagnostics, "vector construction argument");
    }
}

void unsupported_local_shapes_have_no_partial_output()
{
    struct Case {
        std::string_view source;
        std::string_view diagnostic;
    };

    constexpr std::array<Case, 7> cases{
        Case{
            "int main() { string text; return 0; }",
            "initialized local int, string, char, or vector"},
        Case{
            "int main() { char value; return 0; }",
            "initialized local int, string, char, or vector"},
        Case{
            "int main() { vector<int> values; return 0; }",
            "initialized local int, string, char, or vector"},
        Case{
            "int main() { int value; return 0; }",
            "initialized local int, string, char, or vector"},
        Case{
            "int main() { bool value = true; return 0; }",
            "local variables of type 'int', 'string', 'char', or "
            "'vector<T>'"},
        Case{
            "int main() { int value = 1; value = 2; return value; }",
            "only supports '=' for string, char, and vector assignments"},
        Case{
            "int main() { int value = 1; value += 2; return value; }",
            "only supports '=' for string, char, and vector assignments"},
    };

    for (const auto& test_case : cases) {
        const CheckedProgram checked{std::string{test_case.source}};
        tpp::DiagnosticEngine diagnostics;

        const auto generated = generate(checked, diagnostics);

        TPP_CHECK(!generated.has_value());
        TPP_CHECK(diagnostics.has_errors());
        check_has_diagnostic(diagnostics, test_case.diagnostic);
    }
}

void runtime_io_and_builtin_shadowing_use_resolutions()
{
    const auto builtins = generate_source(R"(int input_number() {
    return read_int();
}
string input_text() {
    return read_string();
}
char input_char() {
    return read_char();
}
int main() {
    print(input_number());
    print(input_text());
    print(input_char());
    return 0;
}
)");
    tpp::test::check_contains(
        builtins,
        "return tpp::runtime::read_int();");
    tpp::test::check_contains(
        builtins,
        "return tpp::runtime::read_string();");
    tpp::test::check_contains(
        builtins,
        "return tpp::runtime::read_char();");

    const auto shadowed = generate_source(R"(int read_int() {
    return 17;
}
int len(string value) {
    return 7;
}
string substring(string value, int left, int right) {
    return value;
}
string read_string() {
    return "shadow";
}
char read_char() {
    return 's';
}
int main() {
    print(read_int());
    print(len("x"));
    print(substring("abc", 0, 1));
    print(read_string());
    print(read_char());
    return 0;
}
)");
    TPP_CHECK(
        shadowed.find("tpp::runtime::read_int") == std::string::npos);
    TPP_CHECK(
        shadowed.find("tpp::runtime::len") == std::string::npos);
    TPP_CHECK(
        shadowed.find("tpp::runtime::substring") == std::string::npos);
    TPP_CHECK(
        shadowed.find("tpp::runtime::read_string") == std::string::npos);
    TPP_CHECK(
        shadowed.find("tpp::runtime::read_char") == std::string::npos);
    TPP_CHECK(
        shadowed.find("#include <pseudo/runtime.hpp>")
        == std::string::npos);
    tpp::test::check_contains(
        shadowed,
        "tpp_function_1(std::string{\"x\", 1})");
}

void scalar_runtime_io_and_initialized_int_storage_are_supported()
{
    const auto output = generate_source(R"(int main() {
    int number = read_int();
    string word = read_string();
    char letter = read_char();
    print(number);
    print(true);
    print(letter);
    print(word);
    print("A\0B");
    return number;
}
)");

    tpp::test::check_contains(
        output,
        "#include <pseudo/runtime.hpp>\n");
    tpp::test::check_contains(
        output,
        "std::int64_t tpp_variable_");
    tpp::test::check_contains(
        output,
        " = tpp::runtime::read_int();");
    tpp::test::check_contains(
        output,
        " = tpp::runtime::read_string();");
    tpp::test::check_contains(
        output,
        " = tpp::runtime::read_char();");
    tpp::test::check_contains(
        output,
        "std::cout << std::boolalpha << true << '\\n';");
    tpp::test::check_contains(
        output,
        "std::cout << std::boolalpha << std::string{\"A\\000B\", 3} "
        "<< '\\n';");
    tpp::test::check_contains(
        output,
        "return static_cast<int>(tpp_variable_");

    const auto print_only = generate_source(
        "int main() { print(false); return 0; }");
    TPP_CHECK(
        print_only.find("#include <pseudo/runtime.hpp>")
        == std::string::npos);
}

void malformed_read_int_state_has_no_partial_output()
{
    {
        CheckedProgram checked{R"(int main() {
    int value = read_int();
    print(value);
    return value;
}
)"};
        auto& body = *require_main(checked.program()).body;
        auto& declaration = require_variant<tpp::VariableDeclaration>(
            require_statement(body.items.front()).node);
        TPP_CHECK(declaration.initializer != nullptr);
        auto& call = require_variant<tpp::CallExpression>(
            declaration.initializer->node);
        call.arguments.push_back(nullptr);
        tpp::DiagnosticEngine diagnostics;

        TPP_CHECK(!generate(checked, diagnostics).has_value());
        TPP_CHECK(diagnostics.has_errors());
        check_has_diagnostic(
            diagnostics,
            "builtin 'read_int' has an unexpected number of arguments");
    }

    {
        const CheckedProgram checked{R"(int main() {
    int value = read_int();
    return value;
}
)"};
        const tpp::ResolutionInfo empty_resolutions;
        const auto context = tpp::CppGenerationContext{
            .types = checked.types(),
            .symbols = checked.symbols(),
            .declarations = checked.declarations(),
            .resolutions = empty_resolutions,
            .type_info = checked.type_info(),
        };
        tpp::DiagnosticEngine diagnostics;

        TPP_CHECK(!tpp::generate_cpp(
            checked.program(),
            context,
            diagnostics).has_value());
        check_has_diagnostic(diagnostics, "callee has no resolution");
    }
}

void range_loops_cache_bounds_and_avoid_inclusive_overflow()
{
    CheckedProgram checked{R"(int begin_bound() {
    print("begin");
    return 2;
}
int end_bound() {
    print("end");
    return 4;
}
int main() {
    for value in begin_bound()..end_bound() { print(value); }
    for value in 5..3 { print(value); }
    for value in 3..3 { print(value); }
    for value in 5..=3 { print(value); }
    for value in -2..=-1 { print(value); }
    for value in -9223372036854775808..=-9223372036854775808 {
        print(value);
    }
    for value in 9223372036854775806..9223372036854775807 {
        print(value);
    }
    for value in 9223372036854775806..=9223372036854775807 {
        print(value);
        continue;
    }
    return 0;
}
)"};
    auto& main = require_main(checked.program());
    TPP_CHECK(main.body != nullptr);
    auto& first = require_statement_node<tpp::ForRangeStatement>(
        *main.body,
        0);
    auto& reversed = require_statement_node<tpp::ForRangeStatement>(
        *main.body,
        1);
    auto& negative = require_statement_node<tpp::ForRangeStatement>(
        *main.body,
        4);
    auto& minimum = require_statement_node<tpp::ForRangeStatement>(
        *main.body,
        5);
    auto& maximum_exclusive = require_statement_node<tpp::ForRangeStatement>(
        *main.body,
        6);
    auto& maximum = require_statement_node<tpp::ForRangeStatement>(
        *main.body,
        7);
    auto& equal_exclusive = require_statement_node<tpp::ForRangeStatement>(
        *main.body,
        2);
    auto& reversed_inclusive = require_statement_node<tpp::ForRangeStatement>(
        *main.body,
        3);
    const std::array loops{
        &first,
        &reversed,
        &equal_exclusive,
        &reversed_inclusive,
        &negative,
        &minimum,
        &maximum_exclusive,
        &maximum,
    };

    tpp::DiagnosticEngine diagnostics;
    const auto generated = generate(checked, diagnostics);
    TPP_CHECK(generated.has_value());
    TPP_CHECK(!diagnostics.has_errors());
    const auto& output = *generated;
    TPP_CHECK(output.find("#include <pseudo/runtime.hpp>") == std::string::npos);

    for (const auto* loop : loops) {
        const auto binding = checked.declarations().symbol_for(*loop);
        TPP_CHECK(binding.has_value());
        const auto suffix = std::to_string(binding->value);
        tpp::test::check_contains(
            output,
            "const std::int64_t tpp_range_begin_" + suffix + " = ");
        tpp::test::check_contains(
            output,
            "const std::int64_t tpp_range_end_" + suffix + " = ");
        tpp::test::check_contains(
            output,
            "std::int64_t tpp_variable_" + suffix
                + " = tpp_range_cursor_" + suffix + ";");
    }

    const auto begin_function = checked.declarations().symbol_for(
        require_function(checked.program(), "begin_bound"));
    const auto end_function = checked.declarations().symbol_for(
        require_function(checked.program(), "end_bound"));
    const auto first_binding = checked.declarations().symbol_for(first);
    TPP_CHECK(begin_function.has_value());
    TPP_CHECK(end_function.has_value());
    TPP_CHECK(first_binding.has_value());
    const auto begin_initialization =
        "tpp_range_begin_" + std::to_string(first_binding->value)
        + " = tpp_function_" + std::to_string(begin_function->value) + "();";
    const auto end_initialization =
        "tpp_range_end_" + std::to_string(first_binding->value)
        + " = tpp_function_" + std::to_string(end_function->value) + "();";
    TPP_CHECK_EQ(count_occurrences(output, begin_initialization), std::size_t{1});
    TPP_CHECK_EQ(count_occurrences(output, end_initialization), std::size_t{1});
    TPP_CHECK(output.find(begin_initialization) < output.find(end_initialization));

    const auto maximum_binding = checked.declarations().symbol_for(maximum);
    TPP_CHECK(maximum_binding.has_value());
    const auto maximum_suffix = std::to_string(maximum_binding->value);
    tpp::test::check_contains(
        output,
        "bool tpp_range_active_" + maximum_suffix
            + " = tpp_range_begin_" + maximum_suffix
            + " <= tpp_range_end_" + maximum_suffix + ";");
    tpp::test::check_contains(
        output,
        "tpp_range_active_" + maximum_suffix
            + " = tpp_range_cursor_" + maximum_suffix
            + " != tpp_range_end_" + maximum_suffix
            + ", tpp_range_cursor_" + maximum_suffix
            + " += tpp_range_active_" + maximum_suffix
            + " ? std::int64_t{1} : std::int64_t{0}");
    TPP_CHECK(
        output.find("tpp_range_cursor_" + maximum_suffix + " <=")
        == std::string::npos);
}

void foreach_uses_owned_snapshots_and_typed_value_bindings()
{
    CheckedProgram checked{R"(vector<vector<int>> make_rows() {
    return vector<vector<int>>(2, vector<int>(2, 6));
}
int main() {
    vector<bool> flags = vector<bool>(2, true);
    vector<char> letters = vector<char>(2, 'x');
    vector<string> words = vector<string>(2, "word");
    vector<vector<int>> rows =
        vector<vector<int>>(2, vector<int>(1, 7));
    for flag in flags { print(flag); }
    for letter in letters { print(letter); }
    for word in words { print(word); }
    for value in rows {
        for value in value { print(value); }
    }
    for character in "az" { print(character); }
    for number in vector<int>(2, 4) { print(number); }
    for number in make_rows()[0] { print(number); }
    return 0;
}
)"};
    auto& main = require_main(checked.program());
    TPP_CHECK(main.body != nullptr);
    auto& flags = require_statement_node<tpp::ForEachStatement>(*main.body, 4);
    auto& letters = require_statement_node<tpp::ForEachStatement>(*main.body, 5);
    auto& words = require_statement_node<tpp::ForEachStatement>(*main.body, 6);
    auto& outer = require_statement_node<tpp::ForEachStatement>(*main.body, 7);
    TPP_CHECK(outer.body != nullptr);
    auto& inner = require_statement_node<tpp::ForEachStatement>(*outer.body, 0);
    auto& characters =
        require_statement_node<tpp::ForEachStatement>(*main.body, 8);
    auto& temporary =
        require_statement_node<tpp::ForEachStatement>(*main.body, 9);
    auto& indexed_temporary =
        require_statement_node<tpp::ForEachStatement>(*main.body, 10);
    const std::array loops{
        &flags,
        &letters,
        &words,
        &outer,
        &inner,
        &characters,
        &temporary,
        &indexed_temporary,
    };

    tpp::DiagnosticEngine diagnostics;
    const auto generated = generate(checked, diagnostics);
    TPP_CHECK(generated.has_value());
    TPP_CHECK(!diagnostics.has_errors());
    const auto& output = *generated;

    constexpr std::array<std::string_view, 8> binding_types{
        "bool",
        "char",
        "std::string",
        "std::vector<std::int64_t>",
        "std::int64_t",
        "char",
        "std::int64_t",
        "std::int64_t",
    };
    constexpr std::array<std::string_view, 8> iterable_types{
        "std::vector<bool>",
        "std::vector<char>",
        "std::vector<std::string>",
        "std::vector<std::vector<std::int64_t>>",
        "std::vector<std::int64_t>",
        "std::string",
        "std::vector<std::int64_t>",
        "std::vector<std::int64_t>",
    };

    for (std::size_t index = 0; index < loops.size(); ++index) {
        const auto binding = checked.declarations().symbol_for(*loops[index]);
        TPP_CHECK(binding.has_value());
        TPP_CHECK(checked.type_info().inferred_type(*binding).has_value());
        const auto suffix = std::to_string(binding->value);
        tpp::test::check_contains(
            output,
            "const " + std::string{iterable_types[index]}
                + " tpp_iterable_" + suffix + " = ");
        tpp::test::check_contains(
            output,
            "for ([[maybe_unused]] " + std::string{binding_types[index]}
                + " tpp_variable_" + suffix + " : tpp_iterable_" + suffix
                + ")");
    }

    TPP_CHECK(output.find("auto& tpp_variable_") == std::string::npos);
    TPP_CHECK(output.find("bool& tpp_variable_") == std::string::npos);
}

void foreach_bindings_are_mutable_copies()
{
    const auto output = generate_source(R"(int main() {
    vector<string> words = vector<string>(2, "word");
    for word in words {
        word.push('!');
        print(word);
    }
    print(words[0]);

    vector<vector<int>> rows =
        vector<vector<int>>(2, vector<int>(1, 1));
    for row in rows {
        row[0] = 9;
        print(row[0]);
    }
    print(rows[0][0]);
    return 0;
}
)");

    tpp::test::check_contains(
        output,
        "tpp::runtime::string_push(tpp_variable_");
    tpp::test::check_contains(
        output,
        "tpp::runtime::vector_index(tpp_variable_");
    TPP_CHECK(output.find("for (std::string&") == std::string::npos);
    TPP_CHECK(output.find("for (std::vector<std::int64_t>&")
        == std::string::npos);
}

void nested_loop_blocks_break_and_continue_are_lowered()
{
    const auto output = generate_source(R"(int main() {
    for outer in 0..2 {
        { print(outer); }
        for inner in 10..13 {
            print(inner);
            break;
        }
        for repeated in 0..2 {
            print(repeated);
            continue;
        }
        continue;
    }
    return 0;
}
)");

    TPP_CHECK_EQ(count_occurrences(output, "for (std::int64_t tpp_range_cursor_"),
        std::size_t{3});
    TPP_CHECK_EQ(count_occurrences(output, "break;"), std::size_t{1});
    TPP_CHECK_EQ(count_occurrences(output, "continue;"), std::size_t{2});
    tpp::test::check_contains(output, "            {\n                std::cout");
}

void supported_locals_and_user_calls_work_inside_loop_bodies()
{
    const auto output = generate_source(R"(void consume(
    int number,
    string text,
    char letter,
    vector<int> values
) {
    print(number);
    print(text);
    print(letter);
    print(values[0]);
}

int main() {
    for value in 0..1 {
        int number = value;
        string text = "range";
        char letter = 'r';
        vector<int> values = vector<int>(1, value);
        consume(number, text, letter, values);
    }

    vector<int> source = vector<int>(1, 7);
    for value in source {
        int number = value;
        string text = "each";
        char letter = 'e';
        vector<int> values = vector<int>(1, value);
        consume(number, text, letter, values);
    }
    return 0;
}
)");

    TPP_CHECK_EQ(count_occurrences(output, "tpp_function_0("), std::size_t{4});
    TPP_CHECK_EQ(count_occurrences(output, "std::int64_t tpp_variable_"),
        std::size_t{4});
    TPP_CHECK_EQ(count_occurrences(output, "std::string tpp_variable_"),
        std::size_t{2});
    TPP_CHECK_EQ(count_occurrences(output, "char tpp_variable_"),
        std::size_t{2});
    TPP_CHECK_EQ(count_occurrences(output, "std::vector<std::int64_t> tpp_variable_"),
        std::size_t{3});
    tpp::test::check_contains(output, "            tpp_function_0(");
}

void malformed_and_unsupported_loops_have_no_partial_output()
{
    {
        CheckedProgram checked{
            "int main() { for value in 0..1 { print(value); } return 0; }"};
        auto& main = require_main(checked.program());
        TPP_CHECK(main.body != nullptr);
        auto& loop = require_statement_node<tpp::ForRangeStatement>(
            *main.body,
            0);
        loop.begin.reset();
        tpp::DiagnosticEngine diagnostics;

        TPP_CHECK(!generate(checked, diagnostics).has_value());
        check_has_diagnostic(diagnostics, "malformed AST");
    }

    {
        CheckedProgram checked{
            "int main() { for value in 0..1 { print(value); } return 0; }"};
        auto& main = require_main(checked.program());
        TPP_CHECK(main.body != nullptr);
        auto& loop = require_statement_node<tpp::ForRangeStatement>(
            *main.body,
            0);
        loop.operator_kind = static_cast<tpp::RangeOperator>(99);
        tpp::DiagnosticEngine diagnostics;

        TPP_CHECK(!generate(checked, diagnostics).has_value());
        check_has_diagnostic(diagnostics, "malformed AST");
    }

    {
        CheckedProgram checked{
            "int main() { for value in \"x\" { print(value); } return 0; }"};
        auto& main = require_main(checked.program());
        TPP_CHECK(main.body != nullptr);
        auto& loop = require_statement_node<tpp::ForEachStatement>(
            *main.body,
            0);
        loop.iterable.reset();
        tpp::DiagnosticEngine diagnostics;

        TPP_CHECK(!generate(checked, diagnostics).has_value());
        check_has_diagnostic(diagnostics, "malformed AST");
    }

    {
        const CheckedProgram checked{R"(int main() {
    for value in 0..1 {
        if true {}
        while false {}
    }
    return 0;
}
)"};
        tpp::DiagnosticEngine diagnostics;

        TPP_CHECK(!generate(checked, diagnostics).has_value());
        TPP_CHECK_EQ(diagnostics.error_count(), std::size_t{2});
        check_has_diagnostic(diagnostics, "if statements");
        check_has_diagnostic(diagnostics, "while statements");
    }

    {
        const CheckedProgram checked{
            "int main() { for value in \"x\" { print(value); } return 0; }"};
        const tpp::TypeInfo empty_types;
        const auto context = tpp::CppGenerationContext{
            .types = checked.types(),
            .symbols = checked.symbols(),
            .declarations = checked.declarations(),
            .resolutions = checked.resolutions(),
            .type_info = empty_types,
        };
        tpp::DiagnosticEngine diagnostics;

        TPP_CHECK(!tpp::generate_cpp(
            checked.program(),
            context,
            diagnostics).has_value());
        TPP_CHECK(diagnostics.has_errors());
        check_has_diagnostic(diagnostics, "type");
    }
}

void missing_and_invalid_main_are_diagnosed()
{
    {
        const CheckedProgram empty{""};
        tpp::DiagnosticEngine diagnostics;
        TPP_CHECK(!generate(empty, diagnostics).has_value());
        TPP_CHECK_EQ(diagnostics.error_count(), std::size_t{1});
        TPP_CHECK_EQ(
            diagnostics.diagnostics().front().message,
            std::string{
                "C++ code generation requires a top-level 'int main()' "
                "function"});
    }

    {
        const CheckedProgram invalid{
            "void main(int argument) { print(argument); }"};
        tpp::DiagnosticEngine diagnostics;
        TPP_CHECK(!generate(invalid, diagnostics).has_value());
        TPP_CHECK_EQ(diagnostics.error_count(), std::size_t{1});
        TPP_CHECK_EQ(
            diagnostics.diagnostics().front().message,
            std::string{
                "C++ code generation requires 'main' to have return type "
                "'int' and no parameters"});
        TPP_CHECK(diagnostics.diagnostics().front().primary_span.has_value());
        TPP_CHECK_EQ(
            invalid.sources().slice(
                *diagnostics.diagnostics().front().primary_span),
            std::string_view{"main"});
    }

    {
        const CheckedProgram helper_only{"void helper() {}"};
        tpp::DiagnosticEngine diagnostics;
        TPP_CHECK(!generate(helper_only, diagnostics).has_value());
        TPP_CHECK_EQ(diagnostics.error_count(), std::size_t{1});
        TPP_CHECK_EQ(
            diagnostics.diagnostics().front().message,
            std::string{
                "C++ code generation requires a top-level 'int main()' "
                "function"});
    }
}

void calls_to_main_are_rejected_without_partial_output()
{
    const CheckedProgram checked{R"(int helper() {
    return main();
}
int main() {
    return 0;
}
)"};
    tpp::DiagnosticEngine diagnostics;

    const auto generated = generate(checked, diagnostics);

    TPP_CHECK(!generated.has_value());
    TPP_CHECK_EQ(diagnostics.error_count(), std::size_t{1});
    check_has_diagnostic(diagnostics, "cannot call 'main'");
}

void duplicate_main_is_rejected_before_emission()
{
    tpp::SourceManager sources;
    tpp::DiagnosticEngine semantic_diagnostics;
    const auto source = sources.add_source(
        "duplicate-main.tpp",
        "int main() {} int main() {}");
    tpp::Lexer lexer{source, sources, semantic_diagnostics};
    const auto tokens = lexer.lex();
    TPP_CHECK(!semantic_diagnostics.has_errors());
    tpp::Parser parser{tokens, sources, semantic_diagnostics};
    auto program = parser.parse_program();
    TPP_CHECK(!semantic_diagnostics.has_errors());

    tpp::TypeContext types;
    tpp::SymbolTable symbols;
    tpp::DeclarationInfo declarations;
    tpp::DeclarationCollector collector{
        types,
        symbols,
        declarations,
        semantic_diagnostics,
    };
    TPP_CHECK(!collector.collect(program));

    const tpp::ResolutionInfo resolutions;
    const tpp::TypeInfo type_info;
    const auto context = tpp::CppGenerationContext{
        .types = types,
        .symbols = symbols,
        .declarations = declarations,
        .resolutions = resolutions,
        .type_info = type_info,
    };
    tpp::DiagnosticEngine diagnostics;

    const auto generated = tpp::generate_cpp(
        program,
        context,
        diagnostics);

    TPP_CHECK(!generated.has_value());
    TPP_CHECK_EQ(diagnostics.error_count(), std::size_t{1});
    TPP_CHECK_EQ(
        diagnostics.diagnostics().front().message,
        std::string{
            "C++ code generation requires exactly one top-level 'main' "
            "function"});
}

void unsupported_program_shapes_report_without_partial_output()
{
    struct Case {
        std::string_view source;
        std::string_view diagnostic;
    };

    constexpr std::array<Case, 4> cases{
        Case{
            "int global = 1; int main() { return 0; }",
            "global variables"},
        Case{
            "void helper() { bool local = true; } "
            "int main() { helper(); return 0; }",
            "local variables"},
        Case{
            "int outer() { int inner() { return 1; } return inner(); } "
            "int main() { return outer(); }",
            "nested functions"},
        Case{
            "int helper(int value) { value = 1; return value; } "
            "int main() { return helper(0); }",
            "only supports '=' for string, char, and vector assignments"},
    };

    for (const auto& test_case : cases) {
        const CheckedProgram checked{std::string{test_case.source}};
        tpp::DiagnosticEngine diagnostics;

        const auto generated = generate(checked, diagnostics);

        TPP_CHECK(!generated.has_value());
        TPP_CHECK(diagnostics.has_errors());
        check_has_diagnostic(diagnostics, test_case.diagnostic);
        if (test_case.diagnostic == std::string_view{"nested functions"}) {
            check_has_diagnostic(
                diagnostics,
                "calls to nested functions");
        }
    }
}

void unsupported_body_errors_are_collected_independently()
{
    const CheckedProgram checked{R"(int helper(int parameter) {
    bool local = true;
    print(local);
    if true {}
    while false {}
    { print(parameter); }
    return parameter;
}
int main() { return 0; }
)"};
    tpp::DiagnosticEngine diagnostics;

    const auto generated = generate(checked, diagnostics);

    TPP_CHECK(!generated.has_value());
    TPP_CHECK_EQ(diagnostics.error_count(), std::size_t{5});
    check_has_diagnostic(diagnostics, "local variables");
    check_has_diagnostic(diagnostics, "if statements");
    check_has_diagnostic(diagnostics, "while statements");
    check_has_diagnostic(diagnostics, "nested blocks");
    check_has_diagnostic(diagnostics, "references to local");
}

void read_int_expression_statement_uses_runtime_lowering()
{
    const CheckedProgram checked{R"(void use_builtin() {
    read_int();
}
int main() { return 0; }
)"};
    tpp::DiagnosticEngine diagnostics;

    const auto generated = generate(checked, diagnostics);

    TPP_CHECK(generated.has_value());
    TPP_CHECK(!diagnostics.has_errors());
    tpp::test::check_contains(
        *generated,
        "    tpp::runtime::read_int();\n");
}

void malformed_ast_reports_instead_of_crashing()
{
    {
        CheckedProgram checked{"int main() { print(-1); }"};
        auto& argument = require_print_argument(
            require_main(checked.program()).body->items.front());
        const auto argument_span = argument.span;
        auto& unary = require_variant<tpp::UnaryExpression>(argument.node);
        unary.operand.reset();
        tpp::DiagnosticEngine diagnostics;

        TPP_CHECK(!generate(checked, diagnostics).has_value());
        TPP_CHECK_EQ(diagnostics.error_count(), std::size_t{1});
        TPP_CHECK_EQ(
            diagnostics.diagnostics().front().message,
            std::string{"malformed AST: unary expression has no operand"});
        TPP_CHECK(diagnostics.diagnostics().front().primary_span.has_value());
        const auto diagnostic_span =
            *diagnostics.diagnostics().front().primary_span;
        TPP_CHECK_EQ(diagnostic_span.source, argument_span.source);
        TPP_CHECK_EQ(diagnostic_span.begin, argument_span.begin);
        TPP_CHECK_EQ(diagnostic_span.end, argument_span.end);
    }

    {
        CheckedProgram checked{"int main() { print(1 + 2); }"};
        auto& argument = require_print_argument(
            require_main(checked.program()).body->items.front());
        auto& binary =
            require_variant<tpp::BinaryExpression>(argument.node);
        binary.left.reset();
        tpp::DiagnosticEngine diagnostics;

        TPP_CHECK(!generate(checked, diagnostics).has_value());
        TPP_CHECK_EQ(
            diagnostics.diagnostics().front().message,
            std::string{
                "malformed AST: binary expression is missing an operand"});
    }

    {
        CheckedProgram checked{"int main() { print((1)); }"};
        auto& argument = require_print_argument(
            require_main(checked.program()).body->items.front());
        auto& parenthesized =
            require_variant<tpp::ParenthesizedExpression>(argument.node);
        parenthesized.expression.reset();
        tpp::DiagnosticEngine diagnostics;

        TPP_CHECK(!generate(checked, diagnostics).has_value());
        TPP_CHECK_EQ(
            diagnostics.diagnostics().front().message,
            std::string{
                "malformed AST: parenthesized expression has no expression"});
    }

    {
        CheckedProgram checked{"int main() { return 0; }"};
        auto& main = require_main(checked.program());
        auto& statement = require_statement(main.body->items.front());
        auto& return_statement =
            require_variant<tpp::ReturnStatement>(statement.node);
        return_statement.value.reset();
        tpp::DiagnosticEngine diagnostics;

        TPP_CHECK(!generate(checked, diagnostics).has_value());
        check_has_diagnostic(diagnostics, "requires a value");
    }

    {
        CheckedProgram checked{"int main() {}"};
        require_main(checked.program()).body.reset();
        tpp::DiagnosticEngine diagnostics;

        TPP_CHECK(!generate(checked, diagnostics).has_value());
        TPP_CHECK_EQ(diagnostics.error_count(), std::size_t{1});
        check_has_diagnostic(diagnostics, "has no body");
    }

    {
        CheckedProgram checked{
            "char first(string text) { return text[0]; }"
            "int main() { return 0; }"};
        auto& function = require_function(checked.program(), "first");
        auto& statement = require_statement(function.body->items.front());
        auto& return_statement =
            require_variant<tpp::ReturnStatement>(statement.node);
        TPP_CHECK(return_statement.value != nullptr);
        auto& index = require_variant<tpp::IndexExpression>(
            return_statement.value->node);
        index.base.reset();
        tpp::DiagnosticEngine diagnostics;

        TPP_CHECK(!generate(checked, diagnostics).has_value());
        check_has_diagnostic(diagnostics, "malformed AST");
    }

    {
        CheckedProgram checked{
            "string replace(string text, string other) {"
            "text = other; return text; }"
            "int main() { return 0; }"};
        auto& function = require_function(checked.program(), "replace");
        auto& statement = require_statement(function.body->items.front());
        auto& assignment =
            require_variant<tpp::AssignmentStatement>(statement.node);
        assignment.value.reset();
        tpp::DiagnosticEngine diagnostics;

        TPP_CHECK(!generate(checked, diagnostics).has_value());
        check_has_diagnostic(diagnostics, "malformed AST");
    }

    {
        CheckedProgram checked{
            "int length(string text) { return text.length(); }"
            "int main() { return 0; }"};
        auto& function = require_function(checked.program(), "length");
        auto& statement = require_statement(function.body->items.front());
        auto& return_statement =
            require_variant<tpp::ReturnStatement>(statement.node);
        TPP_CHECK(return_statement.value != nullptr);
        auto& call = require_variant<tpp::CallExpression>(
            return_statement.value->node);
        TPP_CHECK(call.callee != nullptr);
        auto& member = require_variant<tpp::MemberAccessExpression>(
            call.callee->node);
        member.base.reset();
        tpp::DiagnosticEngine diagnostics;

        TPP_CHECK(!generate(checked, diagnostics).has_value());
        check_has_diagnostic(diagnostics, "malformed AST");
    }
}

void malformed_semantic_context_is_diagnosed()
{
    const CheckedProgram checked{"int main() { print(1); }"};

    {
        const tpp::ResolutionInfo empty_resolutions;
        const auto context = tpp::CppGenerationContext{
            .types = checked.types(),
            .symbols = checked.symbols(),
            .declarations = checked.declarations(),
            .resolutions = empty_resolutions,
            .type_info = checked.type_info(),
        };
        tpp::DiagnosticEngine diagnostics;

        TPP_CHECK(!tpp::generate_cpp(
            checked.program(),
            context,
            diagnostics).has_value());
        check_has_diagnostic(diagnostics, "semantic state");
    }

    {
        const tpp::TypeInfo empty_types;
        const auto context = tpp::CppGenerationContext{
            .types = checked.types(),
            .symbols = checked.symbols(),
            .declarations = checked.declarations(),
            .resolutions = checked.resolutions(),
            .type_info = empty_types,
        };
        tpp::DiagnosticEngine diagnostics;

        TPP_CHECK(!tpp::generate_cpp(
            checked.program(),
            context,
            diagnostics).has_value());
        check_has_diagnostic(diagnostics, "semantic state");
    }

    {
        CheckedProgram argument_checked{R"(string combine(string text, char suffix) {
    return text;
}
int main() {
    print(combine("x", 'y'));
    return 0;
}
)"};
        auto& argument = require_print_argument(
            require_main(argument_checked.program()).body->items.front());
        auto& call = require_variant<tpp::CallExpression>(argument.node);
        TPP_CHECK_EQ(call.arguments.size(), std::size_t{2});
        std::swap(call.arguments[0], call.arguments[1]);
        tpp::DiagnosticEngine diagnostics;

        TPP_CHECK(!generate(argument_checked, diagnostics).has_value());
        check_has_diagnostic(
            diagnostics,
            "call argument type does not match resolved function");
    }

    {
        CheckedProgram result_checked{R"(string text() {
    return "x";
}
char character() {
    return 'y';
}
int main() {
    print(text());
    print(character());
    return 0;
}
)"};
        auto& body = *require_main(result_checked.program()).body;
        auto& text_call = require_print_argument(body.items[0]);
        auto& character_call = require_print_argument(body.items[1]);
        std::swap(text_call.node, character_call.node);
        tpp::DiagnosticEngine diagnostics;

        TPP_CHECK(!generate(result_checked, diagnostics).has_value());
        check_has_diagnostic(
            diagnostics,
            "call result type does not match resolved function");
    }
}

void string_semantic_identity_is_required()
{
    {
        const CheckedProgram checked{
            "int main() { print(len(\"x\")); return 0; }"};
        const tpp::ResolutionInfo empty_resolutions;
        const auto context = tpp::CppGenerationContext{
            .types = checked.types(),
            .symbols = checked.symbols(),
            .declarations = checked.declarations(),
            .resolutions = empty_resolutions,
            .type_info = checked.type_info(),
        };
        tpp::DiagnosticEngine diagnostics;

        TPP_CHECK(!tpp::generate_cpp(
            checked.program(),
            context,
            diagnostics).has_value());
        check_has_diagnostic(diagnostics, "callee has no resolution");
    }

    {
        const CheckedProgram checked{
            "char first(string text) { return text[0]; }"
            "int main() { return 0; }"};
        const tpp::TypeInfo empty_types;
        const auto context = tpp::CppGenerationContext{
            .types = checked.types(),
            .symbols = checked.symbols(),
            .declarations = checked.declarations(),
            .resolutions = checked.resolutions(),
            .type_info = empty_types,
        };
        tpp::DiagnosticEngine diagnostics;

        TPP_CHECK(!tpp::generate_cpp(
            checked.program(),
            context,
            diagnostics).has_value());
        check_has_diagnostic(diagnostics, "expression has no type");
    }

    {
        CheckedProgram checked{
            "int length(string text) { return text.length(); }"
            "int main() { return 0; }"};
        auto& function = require_function(checked.program(), "length");
        auto& statement = require_statement(function.body->items.front());
        auto& return_statement =
            require_variant<tpp::ReturnStatement>(statement.node);
        TPP_CHECK(return_statement.value != nullptr);
        auto& call = require_variant<tpp::CallExpression>(
            return_statement.value->node);
        TPP_CHECK(call.callee != nullptr);
        auto& old_member = require_variant<tpp::MemberAccessExpression>(
            call.callee->node);
        auto receiver = std::move(old_member.base);
        const auto callee_span = call.callee->span;
        call.callee = std::make_unique<tpp::Expression>(tpp::Expression{
            callee_span,
            tpp::MemberAccessExpression{
                std::move(receiver),
                "length",
            },
        });
        tpp::DiagnosticEngine diagnostics;

        TPP_CHECK(!generate(checked, diagnostics).has_value());
        check_has_diagnostic(diagnostics, "has no semantic identity");
    }
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
        {"integer defensive validation",
         integer_codegen_defensively_validates_mutated_lexemes},
        {"stable prototypes and definitions",
         top_level_functions_have_stable_prototypes_and_definitions},
        {"all scalar signatures",
         all_scalar_parameter_and_return_types_are_supported},
        {"forward calls recursion and mutual recursion",
         forward_calls_recursion_and_mutual_recursion_use_prototypes},
        {"calls in every supported context",
         calls_work_in_statements_returns_print_and_nested_arguments},
        {"parenthesized references and calls",
         parenthesized_parameters_and_callees_preserve_structure},
        {"safe generated names", generated_names_do_not_copy_cpp_keywords},
        {"user print shadows builtin",
         user_function_named_print_shadows_the_builtin},
        {"string operations use semantic runtime helpers",
         string_operations_use_semantic_runtime_helpers},
        {"initialized string and char locals",
         initialized_string_and_char_locals_are_supported},
        {"temporary strings support indexing and length",
         temporary_strings_support_indexing_and_length},
        {"recursive vector types construction and storage",
         vector_types_construction_and_storage_are_recursive},
        {"vector indexing and assignments use checked runtime",
         vector_indexing_and_assignments_use_checked_runtime},
        {"malformed vector state has no partial output",
         malformed_vector_state_has_no_partial_output},
        {"unsupported local shapes",
         unsupported_local_shapes_have_no_partial_output},
        {"runtime IO and builtin shadowing",
         runtime_io_and_builtin_shadowing_use_resolutions},
        {"scalar runtime IO and initialized int storage",
         scalar_runtime_io_and_initialized_int_storage_are_supported},
        {"malformed read_int state has no partial output",
         malformed_read_int_state_has_no_partial_output},
        {"range loops cache bounds and avoid inclusive overflow",
         range_loops_cache_bounds_and_avoid_inclusive_overflow},
        {"foreach snapshots and typed value bindings",
         foreach_uses_owned_snapshots_and_typed_value_bindings},
        {"foreach bindings are mutable copies",
         foreach_bindings_are_mutable_copies},
        {"nested loop blocks and transfers",
         nested_loop_blocks_break_and_continue_are_lowered},
        {"supported locals and calls inside loops",
         supported_locals_and_user_calls_work_inside_loop_bodies},
        {"malformed and unsupported loops",
         malformed_and_unsupported_loops_have_no_partial_output},
        {"missing and invalid main", missing_and_invalid_main_are_diagnosed},
        {"calls to main", calls_to_main_are_rejected_without_partial_output},
        {"duplicate main", duplicate_main_is_rejected_before_emission},
        {"unsupported program shapes",
         unsupported_program_shapes_report_without_partial_output},
        {"independent unsupported body errors",
         unsupported_body_errors_are_collected_independently},
        {"read_int expression statement",
         read_int_expression_statement_uses_runtime_lowering},
        {"malformed AST", malformed_ast_reports_instead_of_crashing},
        {"malformed semantic context",
         malformed_semantic_context_is_diagnosed},
        {"string semantic identity",
         string_semantic_identity_is_required},
    });
}

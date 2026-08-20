#include "test_support.hpp"

#include "cli.hpp"

#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <span>
#include <string>
#include <string_view>

#ifndef TPP_TEST_DATA_DIR
#error "TPP_TEST_DATA_DIR must point to the tests/data directory"
#endif

namespace {

constexpr std::string_view generated_cpp =
    "#include <cstdint>\n"
    "#include <iostream>\n"
    "#include <string>\n"
    "\n"
    "int main()\n"
    "{\n"
    "    std::cout << std::boolalpha << std::string{\"Hello\", 5} "
    "<< '\\n';\n"
    "    std::cout << std::boolalpha << std::int64_t{42} << '\\n';\n"
    "    return static_cast<int>(std::int64_t{0});\n"
    "}\n";

struct Result {
    int exit_code;
    std::string stdout_text;
    std::string stderr_text;
};

Result invoke(const std::initializer_list<std::string_view> arguments)
{
    std::ostringstream out;
    std::ostringstream err;
    const auto args = std::span<const std::string_view>(
        arguments.begin(),
        arguments.size());

    const auto exit_code = tpp::cli::run(args, out, err);
    return {exit_code, out.str(), err.str()};
}

std::string read_data_file(const std::string_view filename)
{
    const auto path = std::filesystem::path(TPP_TEST_DATA_DIR) / filename;
    std::ifstream input{path, std::ios::binary};
    if (!input) {
        throw tpp::test::Failure{"cannot open test data file '"
                                 + path.string() + "'"};
    }

    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

void no_arguments_is_a_usage_error()
{
    const auto result = invoke({});

    TPP_CHECK_EQ(result.exit_code, 2);
    TPP_CHECK(result.stdout_text.empty());
    TPP_CHECK_EQ(
        result.stderr_text,
        std::string("pseudo: error: no input file\n"));
}

void help_is_printed_to_stdout()
{
    const auto result = invoke({"--help"});

    TPP_CHECK_EQ(result.exit_code, 0);
    TPP_CHECK_EQ(
        result.stdout_text,
        std::string(
            "Usage: pseudo [options] <file.tpp>\n"
            "\n"
            "Options:\n"
            "  -h, --help     Show this help message\n"
            "  -v, --version  Show version information\n"
            "      --dump-ast Print the parsed AST\n"
            "      --emit-cpp Emit generated C++20 source\n"));
    TPP_CHECK(result.stderr_text.empty());
}

void version_is_printed_to_stdout()
{
    const auto result = invoke({"--version"});

    TPP_CHECK_EQ(result.exit_code, 0);
    TPP_CHECK_EQ(result.stdout_text, std::string("Pseudo 0.1.0-alpha\n"));
    TPP_CHECK(result.stderr_text.empty());
}

void unknown_option_is_a_usage_error()
{
    const auto result = invoke({"--wat"});

    TPP_CHECK_EQ(result.exit_code, 2);
    TPP_CHECK(result.stdout_text.empty());
    TPP_CHECK_EQ(
        result.stderr_text,
        std::string("pseudo: error: unknown option '--wat'\n"));
}

void multiple_input_files_are_rejected()
{
    const auto result = invoke({"first.tpp", "second.tpp"});

    TPP_CHECK_EQ(result.exit_code, 2);
    TPP_CHECK(result.stdout_text.empty());
    TPP_CHECK_EQ(
        result.stderr_text,
        std::string("pseudo: error: expected exactly one input file\n"));
}

void existing_empty_file_compiles_successfully()
{
    const auto input = std::filesystem::path(TPP_TEST_DATA_DIR) / "empty.tpp";
    const auto input_text = input.string();
    const auto result = invoke({input_text});

    TPP_CHECK_EQ(result.exit_code, 0);
    TPP_CHECK(result.stdout_text.empty());
    TPP_CHECK(result.stderr_text.empty());
}

void dump_ast_requires_an_input_file()
{
    const auto result = invoke({"--dump-ast"});

    TPP_CHECK_EQ(result.exit_code, 2);
    TPP_CHECK(result.stdout_text.empty());
    TPP_CHECK_EQ(
        result.stderr_text,
        std::string{"pseudo: error: no input file\n"});
}

void dump_ast_accepts_the_flag_before_or_after_the_input()
{
    const auto input = std::filesystem::path(TPP_TEST_DATA_DIR) / "empty.tpp";
    const auto input_text = input.string();

    const auto before = invoke({"--dump-ast", input_text});
    TPP_CHECK_EQ(before.exit_code, 0);
    TPP_CHECK_EQ(before.stdout_text, std::string{"Program\n"});
    TPP_CHECK(before.stderr_text.empty());

    const auto after = invoke({input_text, "--dump-ast"});
    TPP_CHECK_EQ(after.exit_code, 0);
    TPP_CHECK_EQ(after.stdout_text, std::string{"Program\n"});
    TPP_CHECK(after.stderr_text.empty());
}

void dump_ast_prints_a_complete_valid_program()
{
    const auto input =
        std::filesystem::path(TPP_TEST_DATA_DIR) / "valid_lexical.tpp";
    const auto result = invoke({"--dump-ast", input.string()});

    TPP_CHECK_EQ(result.exit_code, 0);
    TPP_CHECK_EQ(
        result.stdout_text,
        std::string{
            "Program\n"
            "  Function name=\"main\" return=int\n"
            "    Parameters\n"
            "    Block\n"
            "      Return\n"
            "        IntegerLiteral value=\"0\"\n"});
    TPP_CHECK(result.stderr_text.empty());
}

void dump_ast_is_suppressed_for_lexical_and_syntax_errors()
{
    const auto lexical =
        std::filesystem::path(TPP_TEST_DATA_DIR) / "invalid_lexical.tpp";
    const auto lexical_result = invoke({"--dump-ast", lexical.string()});
    TPP_CHECK_EQ(lexical_result.exit_code, 1);
    TPP_CHECK(lexical_result.stdout_text.empty());
    TPP_CHECK_EQ(
        lexical_result.stderr_text,
        lexical.string()
            + ":1:1: error: unknown character '@'\n"
              "  1 | @\n"
              "    | ^\n");

    const auto syntax =
        std::filesystem::path(TPP_TEST_DATA_DIR) / "invalid_syntax.tpp";
    const auto syntax_result = invoke({"--dump-ast", syntax.string()});
    TPP_CHECK_EQ(syntax_result.exit_code, 1);
    TPP_CHECK(syntax_result.stdout_text.empty());
    TPP_CHECK_EQ(
        syntax_result.stderr_text,
        syntax.string()
            + ":1:9: error: expected expression\n"
              "  1 | int x = }\n"
              "    |         ^\n");
}

void declaration_errors_suppress_all_output_modes()
{
    const auto input =
        std::filesystem::path(TPP_TEST_DATA_DIR) / "duplicate_declaration.tpp";
    const auto expected_error =
        input.string()
        + ":3:9: error: duplicate declaration of 'value'\n"
          "  3 |     int value;\n"
          "    |         ^~~~~\n"
        + input.string()
        + ":2:9: note: previous declaration is here\n"
          "  2 |     int value;\n"
          "    |         ^~~~~\n";

    const auto normal = invoke({input.string()});
    TPP_CHECK_EQ(normal.exit_code, 1);
    TPP_CHECK(normal.stdout_text.empty());
    TPP_CHECK_EQ(normal.stderr_text, expected_error);

    const auto ast = invoke({"--dump-ast", input.string()});
    TPP_CHECK_EQ(ast.exit_code, 1);
    TPP_CHECK(ast.stdout_text.empty());
    TPP_CHECK_EQ(ast.stderr_text, expected_error);

    const auto cpp = invoke({input.string(), "--emit-cpp"});
    TPP_CHECK_EQ(cpp.exit_code, 1);
    TPP_CHECK(cpp.stdout_text.empty());
    TPP_CHECK_EQ(cpp.stderr_text, expected_error);
}

void name_resolution_errors_suppress_all_output_modes()
{
    const auto input =
        std::filesystem::path(TPP_TEST_DATA_DIR) / "unknown_name.tpp";
    const auto expected_error =
        input.string()
        + ":2:11: error: unknown name 'missing'\n"
          "  2 |     print(missing);\n"
          "    |           ^~~~~~~\n";

    const auto normal = invoke({input.string()});
    TPP_CHECK_EQ(normal.exit_code, 1);
    TPP_CHECK(normal.stdout_text.empty());
    TPP_CHECK_EQ(normal.stderr_text, expected_error);

    const auto ast = invoke({"--dump-ast", input.string()});
    TPP_CHECK_EQ(ast.exit_code, 1);
    TPP_CHECK(ast.stdout_text.empty());
    TPP_CHECK_EQ(ast.stderr_text, expected_error);

    const auto cpp = invoke({input.string(), "--emit-cpp"});
    TPP_CHECK_EQ(cpp.exit_code, 1);
    TPP_CHECK(cpp.stdout_text.empty());
    TPP_CHECK_EQ(cpp.stderr_text, expected_error);
}

void type_errors_suppress_all_output_modes()
{
    const auto input =
        std::filesystem::path(TPP_TEST_DATA_DIR) / "type_error.tpp";
    const auto expected_error =
        input.string()
        + ":2:17: error: cannot initialize 'int' with value of type 'string'\n"
          "  2 |     int value = \"text\";\n"
          "    |                 ^~~~~~\n";

    const auto normal = invoke({input.string()});
    TPP_CHECK_EQ(normal.exit_code, 1);
    TPP_CHECK(normal.stdout_text.empty());
    TPP_CHECK_EQ(normal.stderr_text, expected_error);

    const auto ast = invoke({"--dump-ast", input.string()});
    TPP_CHECK_EQ(ast.exit_code, 1);
    TPP_CHECK(ast.stdout_text.empty());
    TPP_CHECK_EQ(ast.stderr_text, expected_error);

    const auto cpp = invoke({input.string(), "--emit-cpp"});
    TPP_CHECK_EQ(cpp.exit_code, 1);
    TPP_CHECK(cpp.stdout_text.empty());
    TPP_CHECK_EQ(cpp.stderr_text, expected_error);
}

void control_flow_errors_suppress_all_output_modes()
{
    const auto input =
        std::filesystem::path(TPP_TEST_DATA_DIR) / "control_flow_error.tpp";
    const auto expected_error =
        input.string()
        + ":2:5: error: 'break' is only allowed inside a loop\n"
          "  2 |     break;\n"
          "    |     ^~~~~~\n";

    const auto normal = invoke({input.string()});
    TPP_CHECK_EQ(normal.exit_code, 1);
    TPP_CHECK(normal.stdout_text.empty());
    TPP_CHECK_EQ(normal.stderr_text, expected_error);

    const auto ast = invoke({"--dump-ast", input.string()});
    TPP_CHECK_EQ(ast.exit_code, 1);
    TPP_CHECK(ast.stdout_text.empty());
    TPP_CHECK_EQ(ast.stderr_text, expected_error);

    const auto cpp = invoke({input.string(), "--emit-cpp"});
    TPP_CHECK_EQ(cpp.exit_code, 1);
    TPP_CHECK(cpp.stdout_text.empty());
    TPP_CHECK_EQ(cpp.stderr_text, expected_error);
}

void unreachable_warnings_preserve_successful_output_modes()
{
    const auto input =
        std::filesystem::path(TPP_TEST_DATA_DIR) / "unreachable_warning.tpp";
    const auto expected_warning =
        input.string()
        + ":3:5: warning: unreachable statement\n"
          "  3 |     print(1);\n"
          "    |     ^~~~~~~~~\n";
    constexpr std::string_view expected_ast =
        "Program\n"
        "  Function name=\"main\" return=int\n"
        "    Parameters\n"
        "    Block\n"
        "      Return\n"
        "        IntegerLiteral value=\"0\"\n"
        "      ExpressionStatement\n"
        "        Call\n"
        "          Callee\n"
        "            Identifier name=\"print\"\n"
        "          Arguments\n"
        "            IntegerLiteral value=\"1\"\n";
    constexpr std::string_view expected_cpp =
        "#include <cstdint>\n"
        "#include <iostream>\n"
        "#include <string>\n"
        "\n"
        "int main()\n"
        "{\n"
        "    return static_cast<int>(std::int64_t{0});\n"
        "    std::cout << std::boolalpha << std::int64_t{1} << '\\n';\n"
        "}\n";

    const auto normal = invoke({input.string()});
    TPP_CHECK_EQ(normal.exit_code, 0);
    TPP_CHECK(normal.stdout_text.empty());
    TPP_CHECK_EQ(normal.stderr_text, expected_warning);

    const auto ast = invoke({"--dump-ast", input.string()});
    TPP_CHECK_EQ(ast.exit_code, 0);
    TPP_CHECK_EQ(ast.stdout_text, expected_ast);
    TPP_CHECK_EQ(ast.stderr_text, expected_warning);

    const auto cpp = invoke({input.string(), "--emit-cpp"});
    TPP_CHECK_EQ(cpp.exit_code, 0);
    TPP_CHECK_EQ(cpp.stdout_text, expected_cpp);
    TPP_CHECK_EQ(cpp.stderr_text, expected_warning);
}

void emit_cpp_requires_an_input_file()
{
    const auto result = invoke({"--emit-cpp"});

    TPP_CHECK_EQ(result.exit_code, 2);
    TPP_CHECK(result.stdout_text.empty());
    TPP_CHECK_EQ(
        result.stderr_text,
        std::string{"pseudo: error: no input file\n"});
}

void emit_cpp_accepts_the_flag_before_after_and_repeated()
{
    const auto input =
        std::filesystem::path(TPP_TEST_DATA_DIR) / "codegen_minimal.tpp";
    const auto input_text = input.string();

    const auto before = invoke({"--emit-cpp", input_text});
    TPP_CHECK_EQ(before.exit_code, 0);
    TPP_CHECK_EQ(before.stdout_text, generated_cpp);
    TPP_CHECK(before.stderr_text.empty());

    const auto after = invoke({input_text, "--emit-cpp"});
    TPP_CHECK_EQ(after.exit_code, 0);
    TPP_CHECK_EQ(after.stdout_text, generated_cpp);
    TPP_CHECK(after.stderr_text.empty());

    const auto repeated =
        invoke({"--emit-cpp", input_text, "--emit-cpp"});
    TPP_CHECK_EQ(repeated.exit_code, 0);
    TPP_CHECK_EQ(repeated.stdout_text, generated_cpp);
    TPP_CHECK(repeated.stderr_text.empty());
}

void emit_cpp_generates_general_top_level_functions()
{
    const auto input =
        std::filesystem::path(TPP_TEST_DATA_DIR) / "codegen_functions.tpp";
    const auto result = invoke({"--emit-cpp", input.string()});

    TPP_CHECK_EQ(result.exit_code, 0);
    TPP_CHECK_EQ(
        result.stdout_text,
        read_data_file("codegen_functions.expected.cpp"));
    TPP_CHECK(result.stderr_text.empty());
}

void emit_cpp_generates_string_and_char_operations()
{
    const auto input =
        std::filesystem::path(TPP_TEST_DATA_DIR) / "codegen_strings.tpp";
    const auto result = invoke({"--emit-cpp", input.string()});

    TPP_CHECK_EQ(result.exit_code, 0);
    TPP_CHECK_EQ(
        result.stdout_text,
        read_data_file("codegen_strings.expected.cpp"));
    TPP_CHECK(result.stderr_text.empty());
}

void output_modes_are_mutually_exclusive()
{
    const auto input =
        std::filesystem::path(TPP_TEST_DATA_DIR) / "codegen_minimal.tpp";
    constexpr std::string_view expected =
        "pseudo: error: options '--dump-ast' and '--emit-cpp' cannot be used "
        "together\n";

    const auto ast_first =
        invoke({"--dump-ast", "--emit-cpp", input.string()});
    TPP_CHECK_EQ(ast_first.exit_code, 2);
    TPP_CHECK(ast_first.stdout_text.empty());
    TPP_CHECK_EQ(ast_first.stderr_text, expected);

    const auto cpp_first =
        invoke({"--emit-cpp", input.string(), "--dump-ast"});
    TPP_CHECK_EQ(cpp_first.exit_code, 2);
    TPP_CHECK(cpp_first.stdout_text.empty());
    TPP_CHECK_EQ(cpp_first.stderr_text, expected);
}

void emit_cpp_is_suppressed_for_frontend_errors()
{
    const auto lexical =
        std::filesystem::path(TPP_TEST_DATA_DIR) / "invalid_lexical.tpp";
    const auto lexical_result = invoke({"--emit-cpp", lexical.string()});
    TPP_CHECK_EQ(lexical_result.exit_code, 1);
    TPP_CHECK(lexical_result.stdout_text.empty());
    TPP_CHECK_EQ(
        lexical_result.stderr_text,
        lexical.string()
            + ":1:1: error: unknown character '@'\n"
              "  1 | @\n"
              "    | ^\n");

    const auto syntax =
        std::filesystem::path(TPP_TEST_DATA_DIR) / "invalid_syntax.tpp";
    const auto syntax_result = invoke({syntax.string(), "--emit-cpp"});
    TPP_CHECK_EQ(syntax_result.exit_code, 1);
    TPP_CHECK(syntax_result.stdout_text.empty());
    TPP_CHECK_EQ(
        syntax_result.stderr_text,
        syntax.string()
            + ":1:9: error: expected expression\n"
              "  1 | int x = }\n"
              "    |         ^\n");
}

void emit_cpp_is_suppressed_for_codegen_errors()
{
    const auto input =
        std::filesystem::path(TPP_TEST_DATA_DIR) / "codegen_unsupported.tpp";
    const auto result = invoke({"--emit-cpp", input.string()});

    TPP_CHECK_EQ(result.exit_code, 1);
    TPP_CHECK(result.stdout_text.empty());
    TPP_CHECK_EQ(
        result.stderr_text,
        input.string()
            + ":1:14: error: C++ code generation only supports local "
              "variables of type 'string' or 'char' yet\n"
              "  1 | int main() { int value = 0; }\n"
              "    |              ^~~\n");
}

void emit_cpp_rejects_a_frontend_valid_empty_program()
{
    const auto input =
        std::filesystem::path(TPP_TEST_DATA_DIR) / "empty.tpp";
    const auto result = invoke({"--emit-cpp", input.string()});

    TPP_CHECK_EQ(result.exit_code, 1);
    TPP_CHECK(result.stdout_text.empty());
    TPP_CHECK_EQ(
        result.stderr_text,
        input.string()
            + ":1:1: error: C++ code generation requires a top-level 'int "
              "main()' function\n"
              "  1 | \n"
              "    | ^\n");
}

void missing_file_is_a_compilation_error()
{
    const auto input = std::filesystem::path(TPP_TEST_DATA_DIR) / "missing.tpp";
    const auto input_text = input.string();
    const auto result = invoke({input_text});

    TPP_CHECK_EQ(result.exit_code, 1);
    TPP_CHECK(result.stdout_text.empty());
    TPP_CHECK_EQ(
        result.stderr_text,
        std::string("pseudo: error: cannot open '") + input_text + "'\n");
}

}

int main()
{
    return tpp::test::run({
        {"no arguments is a usage error", no_arguments_is_a_usage_error},
        {"help is printed to stdout", help_is_printed_to_stdout},
        {"version is printed to stdout", version_is_printed_to_stdout},
        {"unknown option is a usage error", unknown_option_is_a_usage_error},
        {"multiple input files are rejected", multiple_input_files_are_rejected},
        {"existing empty file compiles successfully",
         existing_empty_file_compiles_successfully},
        {"dump AST requires input", dump_ast_requires_an_input_file},
        {"dump AST option order",
         dump_ast_accepts_the_flag_before_or_after_the_input},
        {"dump AST valid program", dump_ast_prints_a_complete_valid_program},
        {"dump AST suppresses erroneous programs",
         dump_ast_is_suppressed_for_lexical_and_syntax_errors},
        {"declaration errors suppress all output modes",
         declaration_errors_suppress_all_output_modes},
        {"name resolution errors suppress all output modes",
         name_resolution_errors_suppress_all_output_modes},
        {"type errors suppress all output modes",
         type_errors_suppress_all_output_modes},
        {"control-flow errors suppress all output modes",
         control_flow_errors_suppress_all_output_modes},
        {"unreachable warnings preserve successful output modes",
         unreachable_warnings_preserve_successful_output_modes},
        {"emit C++ requires input", emit_cpp_requires_an_input_file},
        {"emit C++ option order and repetition",
         emit_cpp_accepts_the_flag_before_after_and_repeated},
        {"emit C++ general top-level functions",
         emit_cpp_generates_general_top_level_functions},
        {"emit C++ string and char operations",
         emit_cpp_generates_string_and_char_operations},
        {"output modes are mutually exclusive",
         output_modes_are_mutually_exclusive},
        {"emit C++ suppresses frontend errors",
         emit_cpp_is_suppressed_for_frontend_errors},
        {"emit C++ suppresses codegen errors",
         emit_cpp_is_suppressed_for_codegen_errors},
        {"emit C++ rejects empty program",
         emit_cpp_rejects_a_frontend_valid_empty_program},
        {"missing file is a compilation error", missing_file_is_a_compilation_error},
    });
}

#include "test_support.hpp"

#include "cli.hpp"

#include <filesystem>
#include <initializer_list>
#include <sstream>
#include <span>
#include <string>
#include <string_view>

#ifndef TPP_TEST_DATA_DIR
#error "TPP_TEST_DATA_DIR must point to the tests/data directory"
#endif

namespace {

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
            "      --dump-ast Print the parsed AST\n"));
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
        {"missing file is a compilation error", missing_file_is_a_compilation_error},
    });
}

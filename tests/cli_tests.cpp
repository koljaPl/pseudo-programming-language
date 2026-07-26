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
            "  -v, --version  Show version information\n"));
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
        {"existing empty file compiles successfully", existing_empty_file_compiles_successfully},
        {"missing file is a compilation error", missing_file_is_a_compilation_error},
    });
}

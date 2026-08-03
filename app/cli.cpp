#include "cli.hpp"

#include "pseudo/ast/printer.hpp"
#include "pseudo/config.hpp"
#include "pseudo/diagnostics/diagnostic_engine.hpp"
#include "pseudo/driver/compilation_session.hpp"
#include "pseudo/driver/compiler.hpp"

#include <string>

namespace tpp::cli {
namespace {

constexpr std::string_view help_text =
    "Usage: pseudo [options] <file.tpp>\n"
    "\n"
    "Options:\n"
    "  -h, --help     Show this help message\n"
    "  -v, --version  Show version information\n"
    "      --dump-ast Print the parsed AST\n";

constexpr int success_exit_code = 0;
constexpr int compilation_error_exit_code = 1;
constexpr int usage_error_exit_code = 2;

[[nodiscard]] bool is_help_option(std::string_view argument) noexcept {
    return argument == "-h" || argument == "--help";
}

[[nodiscard]] bool is_version_option(std::string_view argument) noexcept {
    return argument == "-v" || argument == "--version";
}

}

ParseResult parse_args(std::span<const std::string_view> args) {
    if (args.empty()) {
        return Error{"no input file"};
    }

    if (args.size() == 1) {
        if (is_help_option(args.front())) {
            return Options{
                .action = Action::help,
                .input = std::nullopt,
            };
        }

        if (is_version_option(args.front())) {
            return Options{
                .action = Action::version,
                .input = std::nullopt,
            };
        }
    }

    Options options;

    for (const std::string_view argument : args) {
        if (argument == "--dump-ast") {
            options.dump_ast = true;
            continue;
        }

        if (argument.starts_with('-')) {
            return Error{"unknown option '" + std::string{argument} + "'"};
        }

        if (options.input.has_value()) {
            return Error{"expected exactly one input file"};
        }

        options.input = std::filesystem::path{std::string{argument}};
    }

    if (!options.input.has_value()) {
        return Error{"no input file"};
    }

    return options;
}

int run(
    std::span<const std::string_view> args,
    std::ostream& out,
    std::ostream& err) {
    const ParseResult result = parse_args(args);

    if (const auto* error = std::get_if<Error>(&result)) {
        err << "pseudo: error: " << error->message << '\n';
        return usage_error_exit_code;
    }

    const Options& options = std::get<Options>(result);

    switch (options.action) {
    case Action::help:
        out << help_text;
        return success_exit_code;
    case Action::version:
        out << "Pseudo " << tpp::version << '\n';
        return success_exit_code;
    case Action::compile:
        break;
    }

    CompilationSession session;
    const Compiler compiler;
    const bool succeeded = compiler.compile(options.input.value(), session);

    render_diagnostics(
        err,
        session.diagnostics().diagnostics(),
        session.sources());

    if (!succeeded) {
        return compilation_error_exit_code;
    }

    if (options.dump_ast) {
        print_ast(out, session.program().value());
    }

    return success_exit_code;
}

}

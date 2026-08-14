#include "cli.hpp"

#include "pseudo/ast/printer.hpp"
#include "pseudo/codegen/cpp_generator.hpp"
#include "pseudo/config.hpp"
#include "pseudo/diagnostics/diagnostic_engine.hpp"
#include "pseudo/driver/compilation_session.hpp"
#include "pseudo/driver/compiler.hpp"

#include <optional>
#include <string>

namespace tpp::cli {
namespace {

constexpr std::string_view help_text =
    "Usage: pseudo [options] <file.tpp>\n"
    "\n"
    "Options:\n"
    "  -h, --help     Show this help message\n"
    "  -v, --version  Show version information\n"
    "      --dump-ast Print the parsed AST\n"
    "      --emit-cpp Emit generated C++20 source\n";

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
            if (options.output_mode == OutputMode::cpp) {
                return Error{
                    "options '--dump-ast' and '--emit-cpp' cannot be used "
                    "together"};
            }

            options.output_mode = OutputMode::ast;
            continue;
        }

        if (argument == "--emit-cpp") {
            if (options.output_mode == OutputMode::ast) {
                return Error{
                    "options '--dump-ast' and '--emit-cpp' cannot be used "
                    "together"};
            }

            options.output_mode = OutputMode::cpp;
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
    bool succeeded = compiler.compile(options.input.value(), session);
    std::optional<std::string> generated_cpp;

    if (succeeded && options.output_mode == OutputMode::cpp) {
        generated_cpp = generate_cpp(
            session.program().value(),
            CppGenerationContext{
                .types = session.types(),
                .symbols = session.symbols(),
                .declarations = session.declarations(),
                .resolutions = session.resolutions(),
                .type_info = session.type_info(),
            },
            session.diagnostics());
        succeeded = generated_cpp.has_value()
            && !session.diagnostics().has_errors();
    }

    render_diagnostics(
        err,
        session.diagnostics().diagnostics(),
        session.sources());

    if (!succeeded) {
        return compilation_error_exit_code;
    }

    if (options.output_mode == OutputMode::ast) {
        print_ast(out, session.program().value());
    } else if (options.output_mode == OutputMode::cpp) {
        out << generated_cpp.value();
    }

    return success_exit_code;
}

}

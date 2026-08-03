#include "pseudo/driver/compiler.hpp"

#include "pseudo/driver/compilation_session.hpp"
#include "pseudo/lexer/lexer.hpp"
#include "pseudo/parser/parser.hpp"

#include <variant>

namespace tpp {

bool Compiler::compile(
    const std::filesystem::path& input_path,
    CompilationSession& session) const {
    session.reset();

    const SourceLoadResult result = session.sources().load_file(input_path);

    if (const auto* error = std::get_if<SourceLoadError>(&result)) {
        session.diagnostics().error(error->message);
        return false;
    }

    const auto source = std::get<SourceId>(result);
    Lexer lexer{source, session.sources(), session.diagnostics()};
    session.tokens() = lexer.lex();

    if (session.diagnostics().has_errors()) {
        return false;
    }

    Parser parser{
        session.tokens(),
        session.sources(),
        session.diagnostics()};
    session.program().emplace(parser.parse_program());

    return !session.diagnostics().has_errors();
}

}

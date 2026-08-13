#include "pseudo/driver/compiler.hpp"

#include "pseudo/driver/compilation_session.hpp"
#include "pseudo/lexer/lexer.hpp"
#include "pseudo/parser/parser.hpp"
#include "pseudo/semantic/declaration_collector.hpp"
#include "pseudo/semantic/name_resolver.hpp"
#include "pseudo/semantic/type_checker.hpp"

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

    if (session.diagnostics().has_errors()) {
        return false;
    }

    DeclarationCollector collector{
        session.types(),
        session.symbols(),
        session.declarations(),
        session.diagnostics()};
    if (!collector.collect(*session.program())) {
        return false;
    }

    NameResolver resolver{
        session.symbols(),
        session.declarations(),
        session.resolutions(),
        session.diagnostics()};
    if (!resolver.resolve(*session.program())) {
        return false;
    }

    TypeChecker type_checker{
        session.types(),
        session.symbols(),
        session.declarations(),
        session.resolutions(),
        session.type_info(),
        session.diagnostics()};
    return type_checker.check(*session.program());
}

}

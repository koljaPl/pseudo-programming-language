#include "pseudo/driver/compiler.hpp"

#include "pseudo/driver/compilation_session.hpp"

#include <variant>

namespace tpp {

bool Compiler::compile(
    const std::filesystem::path& input_path,
    CompilationSession& session) const {
    const SourceLoadResult result = session.sources().load_file(input_path);

    if (const auto* error = std::get_if<SourceLoadError>(&result)) {
        session.diagnostics().error(error->message);
        return false;
    }

    return !session.diagnostics().has_errors();
}

}

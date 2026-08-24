#pragma once

#include <filesystem>

namespace tpp {

class CompilationSession;

class Compiler {
public:
    [[nodiscard]] bool compile(
        const std::filesystem::path& input_path,
        CompilationSession& session) const;
};

}

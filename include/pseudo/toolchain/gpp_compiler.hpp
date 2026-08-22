#pragma once

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace tpp {

struct GppCompilerConfig {
    std::filesystem::path compiler_executable{"g++"};
    std::filesystem::path runtime_include_directory;
    std::chrono::milliseconds timeout{30000};
    std::optional<std::filesystem::path> temporary_root;
};

enum class GppCompilationStatus {
    succeeded,
    compilation_failed,
    compiler_signaled,
    launch_failed,
    timed_out,
    filesystem_error,
    wait_failed,
    invalid_configuration,
    unsupported_platform,
};

class GppCompiler;

class CompiledProgram {
public:
    CompiledProgram(const CompiledProgram&) = delete;
    CompiledProgram& operator=(const CompiledProgram&) = delete;

    CompiledProgram(CompiledProgram&& other) noexcept;
    CompiledProgram& operator=(CompiledProgram&& other) noexcept;
    ~CompiledProgram() noexcept;

    [[nodiscard]] const std::filesystem::path& executable_path() const noexcept;

private:
    friend class GppCompiler;

    CompiledProgram(
        std::filesystem::path workspace_path,
        std::filesystem::path executable_path) noexcept;

    void cleanup() noexcept;

    std::filesystem::path workspace_path_;
    std::filesystem::path executable_path_;
};

struct GppCompilationResult {
    GppCompilationStatus status{GppCompilationStatus::invalid_configuration};
    std::optional<CompiledProgram> program;
    std::optional<int> exit_code;
    std::optional<int> signal;
    std::error_code system_error;
    std::string stdout_text;
    std::string stderr_text;
    std::string message;

    [[nodiscard]] bool succeeded() const noexcept;
};

class GppCompiler {
public:
    explicit GppCompiler(GppCompilerConfig config);

    [[nodiscard]] GppCompilationResult compile(
        std::string_view generated_cpp) const;

private:
    GppCompilerConfig config_;
};

}

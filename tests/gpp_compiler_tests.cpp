#include "test_support.hpp"

#include "pseudo/config.hpp"
#include "pseudo/codegen/cpp_generator.hpp"
#include "pseudo/driver/compilation_session.hpp"
#include "pseudo/driver/compiler.hpp"
#include "pseudo/lowering/lowerer.hpp"
#include "pseudo/toolchain/gpp_compiler.hpp"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#ifndef TPP_PROCESS_TEST_HELPER
#error "TPP_PROCESS_TEST_HELPER must name the fake compiler helper"
#endif

#ifndef TPP_RUNTIME_INCLUDE_DIR
#error "TPP_RUNTIME_INCLUDE_DIR must name the runtime include directory"
#endif

namespace {

using namespace std::chrono_literals;

static_assert(!std::is_copy_constructible_v<tpp::CompiledProgram>);
static_assert(!std::is_copy_assignable_v<tpp::CompiledProgram>);
static_assert(std::is_nothrow_move_constructible_v<tpp::CompiledProgram>);
static_assert(std::is_nothrow_move_assignable_v<tpp::CompiledProgram>);

#if (!defined(__unix__) && !defined(__APPLE__)) \
    || !TPP_HAVE_POSIX_SPAWN_FILE_ACTIONS_ADDCLOSEFROM_NP

void unsupported_platform_is_reported_explicitly()
{
    tpp::GppCompilerConfig config;
    config.runtime_include_directory = ".";
    const tpp::GppCompiler compiler{std::move(config)};
    const auto result = compiler.compile("int main() {}\n");
    TPP_CHECK_EQ(
        result.status,
        tpp::GppCompilationStatus::unsupported_platform);
    TPP_CHECK(!result.succeeded());
    TPP_CHECK(!result.program.has_value());
    TPP_CHECK(!result.message.empty());
}

#else

class ScopedFileDescriptor {
public:
    explicit ScopedFileDescriptor(const int descriptor) noexcept
        : descriptor_{descriptor}
    {
    }

    ScopedFileDescriptor(const ScopedFileDescriptor&) = delete;
    ScopedFileDescriptor& operator=(const ScopedFileDescriptor&) = delete;

    ~ScopedFileDescriptor() noexcept
    {
        if (descriptor_ >= 0) {
            static_cast<void>(close(descriptor_));
        }
    }

    [[nodiscard]] int get() const noexcept { return descriptor_; }

private:
    int descriptor_;
};

class TemporaryDirectory {
public:
    explicit TemporaryDirectory(const std::string_view label)
    {
        const auto root = std::filesystem::temp_directory_path();

#if defined(__unix__) || defined(__APPLE__)
        auto pattern = (root / (std::string{label} + "-XXXXXX")).string();
        std::vector<char> writable(pattern.begin(), pattern.end());
        writable.push_back('\0');
        const char* created = mkdtemp(writable.data());
        if (created == nullptr) {
            throw tpp::test::Failure{
                "mkdtemp failed: "
                + std::error_code{errno, std::generic_category()}.message()};
        }
        path_ = created;
#else
        const auto seed = std::chrono::steady_clock::now()
                              .time_since_epoch()
                              .count();
        for (std::uint32_t attempt = 0; attempt < 1000; ++attempt) {
            const auto candidate = root
                / (std::string{label} + '-' + std::to_string(seed) + '-'
                   + std::to_string(attempt));
            std::error_code error;
            if (std::filesystem::create_directory(candidate, error)) {
                path_ = candidate;
                break;
            }
        }
        if (path_.empty()) {
            throw tpp::test::Failure{"failed to create temporary directory"};
        }
#endif
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    ~TemporaryDirectory() noexcept
    {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return path_;
    }

private:
    std::filesystem::path path_;
};

void write_file(
    const std::filesystem::path& path,
    const std::string_view contents)
{
    std::ofstream stream{path, std::ios::binary | std::ios::trunc};
    TPP_CHECK(stream.is_open());
    stream.write(
        contents.data(),
        static_cast<std::streamsize>(contents.size()));
    TPP_CHECK(static_cast<bool>(stream));
    stream.close();
    TPP_CHECK(static_cast<bool>(stream));
}

bool is_executable_file(const std::filesystem::path& path)
{
    std::error_code error;
    const auto status = std::filesystem::status(path, error);
    if (error || !std::filesystem::is_regular_file(status)) {
        return false;
    }
    constexpr auto executable_bits =
        std::filesystem::perms::owner_exec
        | std::filesystem::perms::group_exec
        | std::filesystem::perms::others_exec;
    return (status.permissions() & executable_bits)
        != std::filesystem::perms::none;
}

void check_permissions(
    const std::filesystem::path& path,
    const std::filesystem::perms expected)
{
    std::error_code error;
    const auto permissions = std::filesystem::status(path, error).permissions();
    TPP_CHECK(!error);
    TPP_CHECK_EQ(
        permissions & std::filesystem::perms::mask,
        expected);
}

class ScopedPathEnvironment {
public:
    explicit ScopedPathEnvironment(const std::filesystem::path& path)
    {
        if (const char* current = std::getenv("PATH")) {
            previous_ = current;
        }
        TPP_CHECK_EQ(::setenv("PATH", path.c_str(), 1), 0);
    }

    ScopedPathEnvironment(const ScopedPathEnvironment&) = delete;
    ScopedPathEnvironment& operator=(const ScopedPathEnvironment&) = delete;

    ~ScopedPathEnvironment() noexcept
    {
        if (previous_.has_value()) {
            static_cast<void>(::setenv("PATH", previous_->c_str(), 1));
        } else {
            static_cast<void>(::unsetenv("PATH"));
        }
    }

private:
    std::optional<std::string> previous_;
};

class FakeCompilerFixture {
public:
    explicit FakeCompilerFixture(
        const std::string_view label = "tpp-gpp-tests")
        : directory_{label},
          compilers_{directory_.path() / "compilers"},
          workspaces_{directory_.path() / "workspaces"}
    {
        std::filesystem::create_directories(compilers_);
        std::filesystem::create_directories(workspaces_);
    }

    [[nodiscard]] std::filesystem::path copy_helper(
        const std::string_view mode)
    {
        const auto destination = compilers_
            / (std::string{mode} + '-' + std::to_string(next_helper_++));
        return copy_helper_to(destination);
    }

    [[nodiscard]] std::filesystem::path copy_helper_to(
        const std::filesystem::path& destination)
    {
        std::filesystem::create_directories(destination.parent_path());
        std::error_code error;
        std::filesystem::copy_file(
            std::filesystem::path{TPP_PROCESS_TEST_HELPER},
            destination,
            std::filesystem::copy_options::overwrite_existing,
            error);
        if (error) {
            throw tpp::test::Failure{
                "failed to copy process helper: " + error.message()};
        }
        std::filesystem::permissions(
            destination,
            std::filesystem::perms::owner_read
                | std::filesystem::perms::owner_write
                | std::filesystem::perms::owner_exec,
            std::filesystem::perm_options::replace,
            error);
        if (error) {
            throw tpp::test::Failure{
                "failed to make process helper executable: "
                + error.message()};
        }
        return destination;
    }

    [[nodiscard]] tpp::GppCompilerConfig config_for(
        std::filesystem::path compiler,
        const std::chrono::milliseconds timeout = 5s) const
    {
        return tpp::GppCompilerConfig{
            .compiler_executable = std::move(compiler),
            .runtime_include_directory =
                std::filesystem::path{TPP_RUNTIME_INCLUDE_DIR},
            .timeout = timeout,
            .temporary_root = workspaces_,
        };
    }

    [[nodiscard]] const std::filesystem::path& root() const noexcept
    {
        return directory_.path();
    }

    [[nodiscard]] const std::filesystem::path& workspaces() const noexcept
    {
        return workspaces_;
    }

    [[nodiscard]] bool workspaces_empty() const
    {
        return std::filesystem::is_empty(workspaces_);
    }

private:
    TemporaryDirectory directory_;
    std::filesystem::path compilers_;
    std::filesystem::path workspaces_;
    std::size_t next_helper_{0};
};

void successful_compilation_owns_and_cleans_artifact()
{
    FakeCompilerFixture fixture;
    std::filesystem::path workspace;
    std::filesystem::path executable;

    {
        const tpp::GppCompiler compiler{
            fixture.config_for(fixture.copy_helper("fake-success"))};
        auto result = compiler.compile("int main() { return 0; }\n");

        TPP_CHECK_EQ(result.status, tpp::GppCompilationStatus::succeeded);
        TPP_CHECK(result.succeeded());
        TPP_CHECK(result.program.has_value());
        TPP_CHECK(result.exit_code.has_value());
        TPP_CHECK_EQ(*result.exit_code, 0);
        TPP_CHECK(!result.signal.has_value());
        TPP_CHECK(!result.system_error);
        TPP_CHECK(result.stdout_text.empty());
        TPP_CHECK(result.stderr_text.empty());
        TPP_CHECK(result.message.empty());

        executable = result.program->executable_path();
        workspace = executable.parent_path();
        TPP_CHECK(std::filesystem::exists(workspace));
        TPP_CHECK(is_executable_file(executable));
        check_permissions(
            workspace,
            std::filesystem::perms::owner_all);
        constexpr auto private_file_permissions =
            std::filesystem::perms::owner_read
            | std::filesystem::perms::owner_write;
        check_permissions(
            workspace / "program.cpp",
            private_file_permissions);
        check_permissions(
            workspace / "compiler.stdout",
            private_file_permissions);
        check_permissions(
            workspace / "compiler.stderr",
            private_file_permissions);
        TPP_CHECK(!fixture.workspaces_empty());
    }

    TPP_CHECK(!std::filesystem::exists(executable));
    TPP_CHECK(!std::filesystem::exists(workspace));
    TPP_CHECK(fixture.workspaces_empty());
}

void nonzero_exit_preserves_raw_output_and_cleans_workspace()
{
    FakeCompilerFixture fixture;
    const tpp::GppCompiler compiler{
        fixture.config_for(fixture.copy_helper("fake-exit-23"))};
    const auto result = compiler.compile("invalid but ignored by fake\n");

    TPP_CHECK_EQ(
        result.status,
        tpp::GppCompilationStatus::compilation_failed);
    TPP_CHECK(!result.succeeded());
    TPP_CHECK(!result.program.has_value());
    TPP_CHECK(result.exit_code.has_value());
    TPP_CHECK_EQ(*result.exit_code, 23);
    TPP_CHECK(!result.signal.has_value());
    TPP_CHECK_EQ(result.stdout_text, std::string{"failure stdout\n"});
    TPP_CHECK_EQ(result.stderr_text, std::string{"failure stderr\n"});
    TPP_CHECK_EQ(
        result.message,
        std::string{"C++ compilation failed with exit code 23"});
    TPP_CHECK(fixture.workspaces_empty());
}

void stdout_and_stderr_are_captured_independently()
{
    FakeCompilerFixture fixture;
    const tpp::GppCompiler compiler{
        fixture.config_for(fixture.copy_helper("fake-output"))};
    const auto result = compiler.compile("int main() {}\n");

    TPP_CHECK(result.succeeded());
    TPP_CHECK_EQ(result.stdout_text, std::string{"compiler stdout\n"});
    TPP_CHECK_EQ(result.stderr_text, std::string{"compiler stderr\n"});
}

void large_stdout_and_stderr_do_not_deadlock()
{
    FakeCompilerFixture fixture;
    const tpp::GppCompiler compiler{
        fixture.config_for(fixture.copy_helper("fake-large-output"), 10s)};
    const auto result = compiler.compile("int main() {}\n");

    constexpr std::size_t expected_size = 1024U * 1024U;
    TPP_CHECK(result.succeeded());
    TPP_CHECK_EQ(result.stdout_text.size(), expected_size);
    TPP_CHECK_EQ(result.stderr_text.size(), expected_size);
    TPP_CHECK(result.stdout_text.find_first_not_of('O') == std::string::npos);
    TPP_CHECK(result.stderr_text.find_first_not_of('E') == std::string::npos);
}

void source_is_written_as_exact_bytes()
{
    FakeCompilerFixture fixture;
    const auto helper = fixture.copy_helper("fake-check-source");
    std::string source{"// exact source bytes\nint main() { return 0; }\n"};
    source.push_back('\0');
    source += "tail";
    write_file(helper.string() + ".expected-source", source);

    const tpp::GppCompiler compiler{fixture.config_for(helper)};
    const auto result = compiler.compile(source);

    TPP_CHECK(result.succeeded());
}

void compiler_argv_and_null_stdin_match_the_contract()
{
    FakeCompilerFixture fixture;
    const auto include_path =
        fixture.root() / "runtime include with spaces ; unicode-ß";
    std::filesystem::create_directory(include_path);
    const auto helper = fixture.copy_helper("fake-check-argv");
    write_file(helper.string() + ".expected-include", include_path.string());

    auto config = fixture.config_for(helper);
    config.runtime_include_directory = include_path;
    const tpp::GppCompiler compiler{std::move(config)};
    const auto result = compiler.compile("int main() {}\n");

    TPP_CHECK(result.succeeded());
}

void compiler_does_not_inherit_caller_file_descriptors()
{
    FakeCompilerFixture fixture;
    const auto marker_path = fixture.root() / "caller-owned-marker";
    const ScopedFileDescriptor marker_descriptor{::open(
        marker_path.c_str(), O_CREAT | O_RDWR | O_TRUNC, 0600)};
    TPP_CHECK(marker_descriptor.get() >= 3);

    const int original_flags = fcntl(marker_descriptor.get(), F_GETFD);
    TPP_CHECK(original_flags >= 0);
    const int inheritable_flags = original_flags & ~FD_CLOEXEC;
    TPP_CHECK_EQ(
        fcntl(
            marker_descriptor.get(),
            F_SETFD,
            inheritable_flags),
        0);

    const auto helper = fixture.copy_helper("fake-check-closed-fd");
    write_file(
        helper.string() + ".expected-closed-fd",
        std::to_string(marker_descriptor.get()));
    const tpp::GppCompiler compiler{fixture.config_for(helper)};
    const auto result = compiler.compile("int main() {}\n");

    TPP_CHECK_EQ(
        fcntl(marker_descriptor.get(), F_GETFD), inheritable_flags);
    TPP_CHECK(result.succeeded());
}

void missing_compiler_is_a_launch_failure()
{
    FakeCompilerFixture fixture;
    const auto missing = fixture.root() / "compiler-does-not-exist";
    const tpp::GppCompiler compiler{fixture.config_for(missing)};
    const auto result = compiler.compile("int main() {}\n");

    TPP_CHECK_EQ(result.status, tpp::GppCompilationStatus::launch_failed);
    TPP_CHECK(!result.program.has_value());
    TPP_CHECK(static_cast<bool>(result.system_error));
    tpp::test::check_contains(
        result.message,
        "C++ compiler could not be started: ");
    TPP_CHECK(fixture.workspaces_empty());
}

void bare_compiler_name_is_resolved_through_path()
{
    FakeCompilerFixture fixture;
    const auto helper = fixture.copy_helper("fake-success");
    const ScopedPathEnvironment path_environment{helper.parent_path()};
    const tpp::GppCompiler compiler{
        fixture.config_for(helper.filename())};
    const auto result = compiler.compile("int main() {}\n");

    TPP_CHECK(result.succeeded());
}

void very_large_timeout_does_not_overflow_the_deadline()
{
    FakeCompilerFixture fixture;
    const tpp::GppCompiler compiler{fixture.config_for(
        fixture.copy_helper("fake-success"),
        std::chrono::milliseconds::max())};
    const auto result = compiler.compile("int main() {}\n");

    TPP_CHECK(result.succeeded());
}

#if defined(__unix__) || defined(__APPLE__)
void signal_termination_is_distinct()
{
    FakeCompilerFixture fixture;
    const tpp::GppCompiler compiler{
        fixture.config_for(fixture.copy_helper("fake-signal"))};
    const auto result = compiler.compile("int main() {}\n");

    TPP_CHECK_EQ(
        result.status,
        tpp::GppCompilationStatus::compiler_signaled);
    TPP_CHECK(!result.exit_code.has_value());
    TPP_CHECK(result.signal.has_value());
    TPP_CHECK_EQ(*result.signal, SIGUSR1);
    TPP_CHECK_EQ(
        result.message,
        std::string{"C++ compiler terminated by signal "}
            + std::to_string(SIGUSR1));
    TPP_CHECK(fixture.workspaces_empty());
}

bool process_no_longer_exists(const pid_t process)
{
    if (kill(process, 0) == 0) {
        return false;
    }
    return errno == ESRCH;
}

void timeout_terminates_the_process_group()
{
    FakeCompilerFixture fixture;
    const auto helper = fixture.copy_helper("fake-timeout-tree");
    const auto pid_path = helper.string() + ".child.pid";
    const tpp::GppCompiler compiler{fixture.config_for(helper, 100ms)};
    const auto result = compiler.compile("int main() {}\n");

    TPP_CHECK_EQ(result.status, tpp::GppCompilationStatus::timed_out);
    TPP_CHECK(!result.program.has_value());
    TPP_CHECK_EQ(
        result.message,
        std::string{"C++ compilation timed out after 100 ms"});
    TPP_CHECK(std::filesystem::exists(pid_path));

    std::ifstream pid_file{pid_path};
    long child_value = -1;
    pid_file >> child_value;
    TPP_CHECK(child_value > 0);
    const auto child = static_cast<pid_t>(child_value);

    bool gone = false;
    for (int attempt = 0; attempt < 100; ++attempt) {
        if (process_no_longer_exists(child)) {
            gone = true;
            break;
        }
        std::this_thread::sleep_for(20ms);
    }
    TPP_CHECK(gone);
    TPP_CHECK(fixture.workspaces_empty());
}
#endif

#if defined(__linux__)
class ScopedSignalDisposition {
public:
    ScopedSignalDisposition(
        const int signal_number,
        void (*const handler)(int))
        : signal_number_{signal_number}
    {
        struct sigaction replacement {};
        replacement.sa_handler = handler;
        sigemptyset(&replacement.sa_mask);
        replacement.sa_flags = 0;
        TPP_CHECK_EQ(
            sigaction(signal_number_, &replacement, &previous_),
            0);
        active_ = true;
    }

    ScopedSignalDisposition(const ScopedSignalDisposition&) = delete;
    ScopedSignalDisposition& operator=(const ScopedSignalDisposition&) = delete;

    ~ScopedSignalDisposition() noexcept
    {
        if (active_) {
            static_cast<void>(sigaction(signal_number_, &previous_, nullptr));
        }
    }

private:
    int signal_number_;
    struct sigaction previous_ {};
    bool active_{false};
};

void auto_reaped_compiler_reports_wait_failure()
{
    FakeCompilerFixture fixture;
    const tpp::GppCompiler compiler{
        fixture.config_for(fixture.copy_helper("fake-success"))};

    tpp::GppCompilationResult result;
    {
        const ScopedSignalDisposition ignored_child{SIGCHLD, SIG_IGN};
        result = compiler.compile("int main() {}\n");
    }

    TPP_CHECK_EQ(result.status, tpp::GppCompilationStatus::wait_failed);
    TPP_CHECK(!result.program.has_value());
    TPP_CHECK_EQ(result.system_error.value(), ECHILD);
    tpp::test::check_contains(
        result.message,
        "failed while waiting for C++ compiler: ");
    TPP_CHECK(fixture.workspaces_empty());
}

void unwritable_system_workspace_reports_filesystem_error()
{
    FakeCompilerFixture fixture;
    auto config = fixture.config_for(fixture.copy_helper("fake-success"));
    config.temporary_root = "/proc";
    const auto result =
        tpp::GppCompiler{std::move(config)}.compile("int main() {}\n");

    TPP_CHECK_EQ(result.status, tpp::GppCompilationStatus::filesystem_error);
    TPP_CHECK(!result.program.has_value());
    TPP_CHECK(static_cast<bool>(result.system_error));
    tpp::test::check_contains(
        result.message,
        "failed to prepare C++ compilation workspace: ");
}
#endif

void invalid_configuration_is_rejected_before_launch()
{
    FakeCompilerFixture fixture;
    const auto helper = fixture.copy_helper("fake-success");

    auto empty_compiler = fixture.config_for(helper);
    empty_compiler.compiler_executable.clear();
    const auto compiler_result =
        tpp::GppCompiler{empty_compiler}.compile("int main() {}\n");
    TPP_CHECK_EQ(
        compiler_result.status,
        tpp::GppCompilationStatus::invalid_configuration);

    auto zero_timeout = fixture.config_for(helper);
    zero_timeout.timeout = 0ms;
    const auto timeout_result =
        tpp::GppCompiler{zero_timeout}.compile("int main() {}\n");
    TPP_CHECK_EQ(
        timeout_result.status,
        tpp::GppCompilationStatus::invalid_configuration);

    auto missing_include = fixture.config_for(helper);
    missing_include.runtime_include_directory =
        fixture.root() / "missing-include";
    const auto include_result =
        tpp::GppCompiler{missing_include}.compile("int main() {}\n");
    TPP_CHECK_EQ(
        include_result.status,
        tpp::GppCompilationStatus::invalid_configuration);

    const auto regular_file = fixture.root() / "not-a-directory";
    write_file(regular_file, "file");
    auto invalid_root = fixture.config_for(helper);
    invalid_root.temporary_root = regular_file;
    const auto root_result =
        tpp::GppCompiler{invalid_root}.compile("int main() {}\n");
    TPP_CHECK_EQ(
        root_result.status,
        tpp::GppCompilationStatus::invalid_configuration);

    TPP_CHECK(!compiler_result.message.empty());
    TPP_CHECK(!timeout_result.message.empty());
    TPP_CHECK(!include_result.message.empty());
    TPP_CHECK(!root_result.message.empty());
    TPP_CHECK(fixture.workspaces_empty());
}

void successful_exit_without_artifact_is_a_filesystem_error()
{
    FakeCompilerFixture fixture;
    const tpp::GppCompiler compiler{
        fixture.config_for(fixture.copy_helper("fake-no-artifact"))};
    const auto result = compiler.compile("int main() {}\n");

    TPP_CHECK_EQ(result.status, tpp::GppCompilationStatus::filesystem_error);
    TPP_CHECK(result.exit_code.has_value());
    TPP_CHECK_EQ(*result.exit_code, 0);
    TPP_CHECK(!result.program.has_value());
    TPP_CHECK(!result.system_error);
    TPP_CHECK_EQ(
        result.message,
        std::string{"C++ compiler did not produce an executable artifact"});
    TPP_CHECK(fixture.workspaces_empty());
}

void compiler_and_workspace_paths_are_not_shell_interpreted()
{
    FakeCompilerFixture fixture{"tpp gpp path ; $ quotes ' unicode-ß"};

#if defined(__unix__) || defined(__APPLE__)
    const std::string marker_name = "tpp_gpp_shell_marker_"
        + std::to_string(static_cast<long long>(getpid()));
#else
    const std::string marker_name = "tpp_gpp_shell_marker";
#endif
    const auto marker = std::filesystem::current_path() / marker_name;
    std::error_code ignored;
    std::filesystem::remove(marker, ignored);

    const auto unusual_directory =
        fixture.root() / "compiler path with spaces ; '$' (ü)";
    const auto helper = fixture.copy_helper_to(
        unusual_directory
        / ("fake-success$(touch${IFS}" + marker_name + ")"));
    const tpp::GppCompiler compiler{fixture.config_for(helper)};
    const auto result = compiler.compile("int main() {}\n");

    TPP_CHECK(result.succeeded());
    TPP_CHECK(!std::filesystem::exists(marker));
}

void sequential_and_concurrent_compilations_use_unique_workspaces()
{
    FakeCompilerFixture fixture;
    const auto helper = fixture.copy_helper("fake-success");
    const tpp::GppCompiler compiler{fixture.config_for(helper, 10s)};

    std::vector<tpp::GppCompilationResult> results;
    results.push_back(compiler.compile("int main() {}\n"));
    results.push_back(compiler.compile("int main() {}\n"));

    std::vector<std::future<tpp::GppCompilationResult>> futures;
    for (int index = 0; index < 4; ++index) {
        futures.push_back(std::async(
            std::launch::async,
            [&compiler] { return compiler.compile("int main() {}\n"); }));
    }
    for (auto& future : futures) {
        results.push_back(future.get());
    }

    std::set<std::filesystem::path> workspaces;
    for (const auto& result : results) {
        TPP_CHECK(result.succeeded());
        TPP_CHECK(result.program.has_value());
        const auto& executable = result.program->executable_path();
        TPP_CHECK(is_executable_file(executable));
        workspaces.insert(executable.parent_path());
    }
    TPP_CHECK_EQ(workspaces.size(), results.size());
}

void compiled_program_moves_transfer_ownership_and_clean_old_artifact()
{
    FakeCompilerFixture fixture;
    const auto helper = fixture.copy_helper("fake-success");
    const tpp::GppCompiler compiler{fixture.config_for(helper)};

    auto first_result = compiler.compile("int main() {}\n");
    auto second_result = compiler.compile("int main() {}\n");
    TPP_CHECK(first_result.program.has_value());
    TPP_CHECK(second_result.program.has_value());

    const auto first_path = first_result.program->executable_path();
    const auto second_path = second_result.program->executable_path();
    {
        tpp::CompiledProgram first = std::move(*first_result.program);
        first_result.program.reset();
        TPP_CHECK(std::filesystem::exists(first_path));

        tpp::CompiledProgram second = std::move(*second_result.program);
        second_result.program.reset();
        TPP_CHECK(std::filesystem::exists(second_path));

        first = std::move(second);
        TPP_CHECK(!std::filesystem::exists(first_path));
        TPP_CHECK(std::filesystem::exists(second_path));
        TPP_CHECK_EQ(first.executable_path(), second_path);
    }

    TPP_CHECK(!std::filesystem::exists(second_path));
    TPP_CHECK(fixture.workspaces_empty());
}

void failure_paths_remove_their_workspaces()
{
    FakeCompilerFixture fixture;

    const auto failing = tpp::GppCompiler{
        fixture.config_for(fixture.copy_helper("fake-exit-23"))}
                             .compile("bad\n");
    TPP_CHECK_EQ(
        failing.status,
        tpp::GppCompilationStatus::compilation_failed);
    TPP_CHECK(fixture.workspaces_empty());

    const auto missing = tpp::GppCompiler{
        fixture.config_for(fixture.root() / "missing-compiler")}
                             .compile("bad\n");
    TPP_CHECK_EQ(missing.status, tpp::GppCompilationStatus::launch_failed);
    TPP_CHECK(fixture.workspaces_empty());

    const auto no_artifact = tpp::GppCompiler{
        fixture.config_for(fixture.copy_helper("fake-no-artifact"))}
                                 .compile("bad\n");
    TPP_CHECK_EQ(
        no_artifact.status,
        tpp::GppCompilationStatus::filesystem_error);
    TPP_CHECK(fixture.workspaces_empty());
}

std::string generate_cpp_from_tppl(
    const std::filesystem::path& source_path,
    tpp::CompilationSession& session)
{
    const tpp::Compiler compiler;
    TPP_CHECK(compiler.compile(source_path, session));
    TPP_CHECK(session.program().has_value());
    TPP_CHECK(!session.diagnostics().has_errors());

    const tpp::LoweringContext context{
        .types = session.types(),
        .symbols = session.symbols(),
        .declarations = session.declarations(),
        .resolutions = session.resolutions(),
        .type_info = session.type_info(),
    };
    auto lowered = tpp::lower_program(
        *session.program(),
        context,
        session.diagnostics());
    TPP_CHECK(lowered.has_value());

    auto generated = tpp::generate_cpp(
        *lowered,
        session.types(),
        session.diagnostics());
    TPP_CHECK(generated.has_value());
    TPP_CHECK(!session.diagnostics().has_errors());
    return std::move(*generated);
}

void run_actual_gxx_suite(
    const std::filesystem::path& compiler_path,
    const std::filesystem::path& runtime_include)
{
    TemporaryDirectory directory{"tpp-actual-gxx"};
    const auto workspaces = directory.path() / "workspaces";
    std::filesystem::create_directory(workspaces);
    const tpp::GppCompiler compiler{tpp::GppCompilerConfig{
        .compiler_executable = compiler_path,
        .runtime_include_directory = runtime_include,
        .timeout = 30s,
        .temporary_root = workspaces,
    }};

    auto minimal = compiler.compile("int main() { return 0; }\n");
    TPP_CHECK(minimal.succeeded());
    TPP_CHECK(minimal.program.has_value());
    TPP_CHECK(is_executable_file(minimal.program->executable_path()));

    auto runtime = compiler.compile(R"(#include <pseudo/runtime.hpp>
#include <string>

int main()
{
    const std::string text{"a\0b", 3};
    return tpp::runtime::string_length(text) == 3 ? 0 : 1;
}
)");
    TPP_CHECK(runtime.succeeded());
    TPP_CHECK(runtime.program.has_value());
    TPP_CHECK(is_executable_file(runtime.program->executable_path()));

    const auto invalid = compiler.compile("int main( {\n");
    TPP_CHECK_EQ(
        invalid.status,
        tpp::GppCompilationStatus::compilation_failed);
    TPP_CHECK(invalid.exit_code.has_value());
    TPP_CHECK(*invalid.exit_code != 0);
    TPP_CHECK(!invalid.stderr_text.empty());
    tpp::test::check_contains(
        invalid.message,
        "C++ compilation failed with exit code ");

    const auto tppl_path = directory.path() / "full-boundary.tpp";
    write_file(tppl_path, R"(int twice(int value) {
    return value * 2;
}

int main() {
    vector<int> values = vector<int>(2, 7);
    for value in values {
        print(twice(value));
    }
    return 0;
}
)");
    tpp::CompilationSession session;
    const auto generated = generate_cpp_from_tppl(tppl_path, session);
    auto boundary = compiler.compile(generated);
    TPP_CHECK(boundary.succeeded());
    TPP_CHECK(boundary.program.has_value());
    TPP_CHECK(is_executable_file(boundary.program->executable_path()));
}

int run_actual_mode(const int argc, char* argv[])
{
    if (argc != 4 || std::string_view{argv[1]} != "--actual-gxx") {
        std::cerr
            << "usage: gpp_compiler_tests --actual-gxx <g++> <runtime-include>\n";
        return 2;
    }

    try {
        run_actual_gxx_suite(argv[2], argv[3]);
    } catch (const std::exception& exception) {
        std::cerr << "[FAIL] actual g++ integration\n       "
                  << exception.what() << '\n';
        return 1;
    } catch (...) {
        std::cerr << "[FAIL] actual g++ integration\n"
                     "       unknown exception\n";
        return 1;
    }

    std::cout << "actual g++ integration passed\n";
    return 0;
}

#endif

} // namespace

int main(const int argc, char* argv[])
{
#if (!defined(__unix__) && !defined(__APPLE__)) \
    || !TPP_HAVE_POSIX_SPAWN_FILE_ACTIONS_ADDCLOSEFROM_NP
    static_cast<void>(argv);
    if (argc != 1) {
        return 2;
    }
    return tpp::test::run({
        {"unsupported platform is reported explicitly",
         unsupported_platform_is_reported_explicitly},
    });
#else
    if (argc != 1) {
        return run_actual_mode(argc, argv);
    }

    return tpp::test::run({
        {"successful compilation owns and cleans artifact",
         successful_compilation_owns_and_cleans_artifact},
        {"nonzero exit preserves raw output and cleans workspace",
         nonzero_exit_preserves_raw_output_and_cleans_workspace},
        {"stdout and stderr are captured independently",
         stdout_and_stderr_are_captured_independently},
        {"large stdout and stderr do not deadlock",
         large_stdout_and_stderr_do_not_deadlock},
        {"source is written as exact bytes", source_is_written_as_exact_bytes},
        {"compiler argv and null stdin match the contract",
         compiler_argv_and_null_stdin_match_the_contract},
        {"compiler does not inherit caller file descriptors",
         compiler_does_not_inherit_caller_file_descriptors},
        {"missing compiler is a launch failure",
         missing_compiler_is_a_launch_failure},
        {"bare compiler name is resolved through PATH",
         bare_compiler_name_is_resolved_through_path},
        {"very large timeout does not overflow the deadline",
         very_large_timeout_does_not_overflow_the_deadline},
#if defined(__unix__) || defined(__APPLE__)
        {"signal termination is distinct", signal_termination_is_distinct},
        {"timeout terminates the process group",
         timeout_terminates_the_process_group},
#endif
#if defined(__linux__)
        {"auto-reaped compiler reports wait failure",
         auto_reaped_compiler_reports_wait_failure},
        {"unwritable system workspace reports filesystem error",
         unwritable_system_workspace_reports_filesystem_error},
#endif
        {"invalid configuration is rejected before launch",
         invalid_configuration_is_rejected_before_launch},
        {"successful exit without artifact is a filesystem error",
         successful_exit_without_artifact_is_a_filesystem_error},
        {"compiler and workspace paths are not shell interpreted",
         compiler_and_workspace_paths_are_not_shell_interpreted},
        {"sequential and concurrent compilations use unique workspaces",
         sequential_and_concurrent_compilations_use_unique_workspaces},
        {"compiled program moves transfer ownership and clean old artifact",
         compiled_program_moves_transfer_ownership_and_clean_old_artifact},
        {"failure paths remove their workspaces",
         failure_paths_remove_their_workspaces},
    });
#endif
}

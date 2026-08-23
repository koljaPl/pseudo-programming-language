#include "pseudo/toolchain/gpp_compiler.hpp"

#include "pseudo/config.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <limits>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if (defined(__unix__) || defined(__APPLE__)) \
    && TPP_HAVE_POSIX_SPAWN_FILE_ACTIONS_ADDCLOSEFROM_NP
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;
#endif

namespace tpp {

CompiledProgram::CompiledProgram(
    std::filesystem::path workspace_path,
    std::filesystem::path executable_path) noexcept
    : workspace_path_(std::move(workspace_path)),
      executable_path_(std::move(executable_path))
{
}

CompiledProgram::CompiledProgram(CompiledProgram&& other) noexcept
    : workspace_path_(std::move(other.workspace_path_)),
      executable_path_(std::move(other.executable_path_))
{
    other.workspace_path_.clear();
    other.executable_path_.clear();
}

CompiledProgram& CompiledProgram::operator=(CompiledProgram&& other) noexcept
{
    if (this == &other) {
        return *this;
    }

    cleanup();
    workspace_path_ = std::move(other.workspace_path_);
    executable_path_ = std::move(other.executable_path_);
    other.workspace_path_.clear();
    other.executable_path_.clear();
    return *this;
}

CompiledProgram::~CompiledProgram() noexcept
{
    cleanup();
}

const std::filesystem::path& CompiledProgram::executable_path() const noexcept
{
    return executable_path_;
}

void CompiledProgram::cleanup() noexcept
{
    if (!workspace_path_.empty()) {
        std::error_code ignored;
        std::filesystem::remove_all(workspace_path_, ignored);
    }

    workspace_path_.clear();
    executable_path_.clear();
}

bool GppCompilationResult::succeeded() const noexcept
{
    return status == GppCompilationStatus::succeeded && program.has_value();
}

GppCompiler::GppCompiler(GppCompilerConfig config)
    : config_(std::move(config))
{
}

namespace {

GppCompilationResult make_result(
    const GppCompilationStatus status,
    std::string message,
    const std::error_code system_error = {})
{
    GppCompilationResult result;
    result.status = status;
    result.system_error = system_error;
    result.message = std::move(message);
    return result;
}

#if (defined(__unix__) || defined(__APPLE__)) \
    && TPP_HAVE_POSIX_SPAWN_FILE_ACTIONS_ADDCLOSEFROM_NP

std::string with_system_error(
    const std::string_view prefix,
    const std::error_code error)
{
    std::string message(prefix);
    message += ": ";
    message += error.message();
    return message;
}

class ScopedWorkspace {
public:
    explicit ScopedWorkspace(std::filesystem::path path) noexcept
        : path_(std::move(path))
    {
    }

    ScopedWorkspace(const ScopedWorkspace&) = delete;
    ScopedWorkspace& operator=(const ScopedWorkspace&) = delete;

    ScopedWorkspace(ScopedWorkspace&& other) noexcept
        : path_(std::move(other.path_))
    {
        other.path_.clear();
    }

    ScopedWorkspace& operator=(ScopedWorkspace&&) = delete;

    ~ScopedWorkspace() noexcept
    {
        if (!path_.empty()) {
            std::error_code ignored;
            std::filesystem::remove_all(path_, ignored);
        }
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return path_;
    }

    [[nodiscard]] std::filesystem::path release() noexcept
    {
        auto released = std::move(path_);
        path_.clear();
        return released;
    }

private:
    std::filesystem::path path_;
};

class FileDescriptor {
public:
    explicit FileDescriptor(const int descriptor = -1) noexcept
        : descriptor_(descriptor)
    {
    }

    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;

    FileDescriptor(FileDescriptor&& other) noexcept
        : descriptor_(std::exchange(other.descriptor_, -1))
    {
    }

    FileDescriptor& operator=(FileDescriptor&& other) noexcept
    {
        if (this != &other) {
            close_ignoring_errors();
            descriptor_ = std::exchange(other.descriptor_, -1);
        }
        return *this;
    }

    ~FileDescriptor() noexcept
    {
        close_ignoring_errors();
    }

    [[nodiscard]] int get() const noexcept { return descriptor_; }

    [[nodiscard]] int release() noexcept
    {
        return std::exchange(descriptor_, -1);
    }

private:
    void close_ignoring_errors() noexcept
    {
        if (descriptor_ >= 0) {
            static_cast<void>(::close(descriptor_));
            descriptor_ = -1;
        }
    }

    int descriptor_;
};

class SpawnFileActions {
public:
    SpawnFileActions() noexcept
        : initialization_error_(::posix_spawn_file_actions_init(&actions_))
    {
    }

    SpawnFileActions(const SpawnFileActions&) = delete;
    SpawnFileActions& operator=(const SpawnFileActions&) = delete;

    ~SpawnFileActions() noexcept
    {
        if (initialization_error_ == 0) {
            static_cast<void>(::posix_spawn_file_actions_destroy(&actions_));
        }
    }

    [[nodiscard]] int initialization_error() const noexcept
    {
        return initialization_error_;
    }

    [[nodiscard]] posix_spawn_file_actions_t* get() noexcept
    {
        return &actions_;
    }

private:
    posix_spawn_file_actions_t actions_{};
    int initialization_error_;
};

class SpawnAttributes {
public:
    SpawnAttributes() noexcept
        : initialization_error_(::posix_spawnattr_init(&attributes_))
    {
    }

    SpawnAttributes(const SpawnAttributes&) = delete;
    SpawnAttributes& operator=(const SpawnAttributes&) = delete;

    ~SpawnAttributes() noexcept
    {
        if (initialization_error_ == 0) {
            static_cast<void>(::posix_spawnattr_destroy(&attributes_));
        }
    }

    [[nodiscard]] int initialization_error() const noexcept
    {
        return initialization_error_;
    }

    [[nodiscard]] posix_spawnattr_t* get() noexcept { return &attributes_; }

private:
    posix_spawnattr_t attributes_{};
    int initialization_error_;
};

struct WaitResult {
    bool timed_out{false};
    bool wait_failed{false};
    int status{0};
    std::error_code system_error;
};

std::error_code errno_error() noexcept
{
    return {errno, std::generic_category()};
}

std::optional<std::filesystem::path> absolute_path(
    const std::filesystem::path& path,
    std::error_code& error)
{
    auto absolute = std::filesystem::absolute(path, error);
    if (error) {
        return std::nullopt;
    }
    return absolute;
}

bool path_is_directory(
    const std::filesystem::path& path,
    std::error_code& error)
{
    const auto status = std::filesystem::status(path, error);
    return !error && std::filesystem::is_directory(status);
}

std::optional<ScopedWorkspace> create_workspace(
    const std::filesystem::path& temporary_root,
    std::error_code& error)
{
    const auto template_path = temporary_root / "tpp-gpp-XXXXXX";
    const auto template_text = template_path.string();
    std::vector<char> writable_template(
        template_text.begin(), template_text.end());
    writable_template.push_back('\0');

    errno = 0;
    char* const created = ::mkdtemp(writable_template.data());
    if (created == nullptr) {
        error = errno_error();
        return std::nullopt;
    }

    std::filesystem::path workspace_path(created);
    ScopedWorkspace workspace(workspace_path);
    if (::chmod(workspace_path.c_str(), S_IRWXU) != 0) {
        error = errno_error();
        return std::nullopt;
    }

    return workspace;
}

bool close_checked(FileDescriptor& descriptor, std::error_code& error) noexcept
{
    const int raw_descriptor = descriptor.release();
    if (raw_descriptor < 0) {
        return true;
    }

    if (::close(raw_descriptor) == 0) {
        return true;
    }

    error = errno_error();
    return false;
}

bool write_all(
    const std::filesystem::path& path,
    const std::string_view bytes,
    std::error_code& error)
{
    const int raw_descriptor = ::open(
        path.c_str(),
        O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC,
        S_IRUSR | S_IWUSR);
    if (raw_descriptor < 0) {
        error = errno_error();
        return false;
    }

    FileDescriptor descriptor(raw_descriptor);
    if (::fchmod(descriptor.get(), S_IRUSR | S_IWUSR) != 0) {
        error = errno_error();
        return false;
    }

    std::size_t written = 0;
    while (written < bytes.size()) {
        constexpr auto maximum_write_size = static_cast<std::size_t>(
            std::numeric_limits<ssize_t>::max());
        const auto remaining = bytes.size() - written;
        const auto write_size = std::min(remaining, maximum_write_size);
        const auto write_result = ::write(
            descriptor.get(),
            bytes.data() + written,
            write_size);
        if (write_result > 0) {
            written += static_cast<std::size_t>(write_result);
            continue;
        }
        if (write_result < 0 && errno == EINTR) {
            continue;
        }

        error = write_result < 0
            ? errno_error()
            : std::make_error_code(std::errc::io_error);
        return false;
    }

    return close_checked(descriptor, error);
}

std::optional<FileDescriptor> create_capture_file(
    const std::filesystem::path& path,
    std::error_code& error)
{
    const int descriptor = ::open(
        path.c_str(),
        O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC,
        S_IRUSR | S_IWUSR);
    if (descriptor < 0) {
        error = errno_error();
        return std::nullopt;
    }

    FileDescriptor capture(descriptor);
    if (::fchmod(capture.get(), S_IRUSR | S_IWUSR) != 0) {
        error = errno_error();
        return std::nullopt;
    }
    return capture;
}

bool move_away_from_standard_descriptors(
    FileDescriptor& descriptor,
    std::error_code& error) noexcept
{
    if (descriptor.get() > STDERR_FILENO) {
        return true;
    }

    const int duplicated = ::fcntl(descriptor.get(), F_DUPFD_CLOEXEC, 3);
    if (duplicated < 0) {
        error = errno_error();
        return false;
    }

    FileDescriptor replacement(duplicated);
    if (!close_checked(descriptor, error)) {
        return false;
    }
    descriptor = std::move(replacement);
    return true;
}

bool read_all(
    const std::filesystem::path& path,
    std::string& contents,
    std::error_code& error)
{
    const int raw_descriptor = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (raw_descriptor < 0) {
        error = errno_error();
        return false;
    }

    FileDescriptor descriptor(raw_descriptor);
    char buffer[8192];
    while (true) {
        const auto read_result = ::read(descriptor.get(), buffer, sizeof(buffer));
        if (read_result > 0) {
            contents.append(buffer, static_cast<std::size_t>(read_result));
            continue;
        }
        if (read_result == 0) {
            break;
        }
        if (errno == EINTR) {
            continue;
        }

        error = errno_error();
        return false;
    }

    return close_checked(descriptor, error);
}

int add_duplication_actions(
    SpawnFileActions& actions,
    const int source,
    const int destination) noexcept
{
    int error = ::posix_spawn_file_actions_adddup2(
        actions.get(), source, destination);
    if (error != 0) {
        return error;
    }

    if (source != destination) {
        error = ::posix_spawn_file_actions_addclose(actions.get(), source);
    }
    return error;
}

int spawn_compiler(
    const std::filesystem::path& executable,
    std::vector<std::string>& arguments,
    const int standard_input,
    const int standard_output,
    const int standard_error,
    pid_t& process_id)
{
    SpawnFileActions actions;
    if (actions.initialization_error() != 0) {
        return actions.initialization_error();
    }

    int setup_error = add_duplication_actions(
        actions, standard_input, STDIN_FILENO);
    if (setup_error == 0) {
        setup_error = add_duplication_actions(
            actions, standard_output, STDOUT_FILENO);
    }
    if (setup_error == 0) {
        setup_error = add_duplication_actions(
            actions, standard_error, STDERR_FILENO);
    }
    if (setup_error == 0) {
        // Keep this action last: the child needs the capture descriptors for
        // the preceding dup2 actions, then must inherit nothing beyond stdio.
        setup_error = ::posix_spawn_file_actions_addclosefrom_np(
            actions.get(), STDERR_FILENO + 1);
    }
    if (setup_error != 0) {
        return setup_error;
    }

    SpawnAttributes attributes;
    if (attributes.initialization_error() != 0) {
        return attributes.initialization_error();
    }

    setup_error = ::posix_spawnattr_setpgroup(attributes.get(), 0);
    if (setup_error == 0) {
        setup_error = ::posix_spawnattr_setflags(
            attributes.get(), POSIX_SPAWN_SETPGROUP);
    }
    if (setup_error != 0) {
        return setup_error;
    }

    std::vector<char*> argument_pointers;
    argument_pointers.reserve(arguments.size() + 1);
    for (auto& argument : arguments) {
        argument_pointers.push_back(argument.data());
    }
    argument_pointers.push_back(nullptr);

    return ::posix_spawnp(
        &process_id,
        executable.c_str(),
        actions.get(),
        attributes.get(),
        argument_pointers.data(),
        environ);
}

void sleep_briefly(const std::chrono::milliseconds duration)
{
    if (duration > std::chrono::milliseconds::zero()) {
        std::this_thread::sleep_for(duration);
    }
}

void terminate_process_group(
    const pid_t process_id,
    int& parent_status) noexcept
{
    static_cast<void>(::kill(-process_id, SIGTERM));

    // Keep the group leader unreaped throughout the grace period. Its zombie
    // retains the numeric PID/PGID if it exits after SIGTERM, so the final
    // negative-PID kill cannot target an unrelated, newly reused process group.
    sleep_briefly(std::chrono::milliseconds{250});

    // Always signal the entire group after the grace period. The compiler
    // parent may already be a zombie while a descendant still ignores SIGTERM.
    static_cast<void>(::kill(-process_id, SIGKILL));

    while (true) {
        const pid_t wait_result = ::waitpid(process_id, &parent_status, 0);
        if (wait_result == process_id) {
            break;
        } else if (wait_result < 0 && errno == EINTR) {
            continue;
        } else {
            break;
        }
    }
}

WaitResult wait_for_process(
    const pid_t process_id,
    const std::chrono::milliseconds timeout)
{
    const auto started_at = std::chrono::steady_clock::now();
    while (true) {
        int process_status = 0;
        const pid_t wait_result = ::waitpid(process_id, &process_status, WNOHANG);
        if (wait_result == process_id) {
            WaitResult result;
            result.status = process_status;
            return result;
        }
        if (wait_result < 0) {
            if (errno == EINTR) {
                continue;
            }

            const auto wait_error = errno_error();
            // ECHILD means the leader has already been reaped externally. Its
            // numeric PGID may therefore be stale and must not be signalled.
            if (wait_error.value() != ECHILD) {
                terminate_process_group(process_id, process_status);
            }
            WaitResult result;
            result.wait_failed = true;
            result.status = process_status;
            result.system_error = wait_error;
            return result;
        }

        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - started_at);
        if (elapsed >= timeout) {
            terminate_process_group(process_id, process_status);
            WaitResult result;
            result.timed_out = true;
            result.status = process_status;
            return result;
        }

        const auto remaining = timeout - elapsed;
        sleep_briefly(
            remaining < std::chrono::milliseconds{5}
                ? remaining
                : std::chrono::milliseconds{5});
    }
}

bool is_executable_regular_file(
    const std::filesystem::path& path,
    std::error_code& error) noexcept
{
    struct stat file_status {};
    if (::lstat(path.c_str(), &file_status) != 0) {
        if (errno != ENOENT) {
            error = errno_error();
        }
        return false;
    }

    if (!S_ISREG(file_status.st_mode)) {
        return false;
    }

    constexpr mode_t executable_bits = S_IXUSR | S_IXGRP | S_IXOTH;
    if ((file_status.st_mode & executable_bits) == 0) {
        return false;
    }

    if (::access(path.c_str(), X_OK) != 0) {
        error = errno_error();
        return false;
    }
    return true;
}

#endif

}

GppCompilationResult GppCompiler::compile(
    const std::string_view generated_cpp) const
{
#if (!defined(__unix__) && !defined(__APPLE__)) \
    || !TPP_HAVE_POSIX_SPAWN_FILE_ACTIONS_ADDCLOSEFROM_NP
    static_cast<void>(generated_cpp);
    return make_result(
        GppCompilationStatus::unsupported_platform,
        "reliable C++ compiler execution is not supported on this platform");
#else
    if (config_.compiler_executable.empty()) {
        return make_result(
            GppCompilationStatus::invalid_configuration,
            "compiler executable must not be empty");
    }
    if (config_.runtime_include_directory.empty()) {
        return make_result(
            GppCompilationStatus::invalid_configuration,
            "runtime include directory must not be empty");
    }
    if (config_.timeout <= std::chrono::milliseconds::zero()) {
        return make_result(
            GppCompilationStatus::invalid_configuration,
            "compile timeout must be greater than zero");
    }
    if (config_.temporary_root.has_value()
        && config_.temporary_root->empty()) {
        return make_result(
            GppCompilationStatus::invalid_configuration,
            "temporary root must not be empty");
    }

    std::error_code filesystem_error;
    const auto runtime_include = absolute_path(
        config_.runtime_include_directory, filesystem_error);
    if (!runtime_include.has_value()) {
        return make_result(
            GppCompilationStatus::invalid_configuration,
            with_system_error(
                "runtime include directory is invalid", filesystem_error),
            filesystem_error);
    }
    if (!path_is_directory(*runtime_include, filesystem_error)) {
        const std::string message = filesystem_error
            ? with_system_error(
                "runtime include directory is invalid", filesystem_error)
            : "runtime include directory is not a directory";
        return make_result(
            GppCompilationStatus::invalid_configuration,
            message,
            filesystem_error);
    }

    std::filesystem::path temporary_root;
    if (config_.temporary_root.has_value()) {
        const auto absolute_root = absolute_path(
            *config_.temporary_root, filesystem_error);
        if (!absolute_root.has_value()) {
            return make_result(
                GppCompilationStatus::invalid_configuration,
                with_system_error("temporary root is invalid", filesystem_error),
                filesystem_error);
        }
        temporary_root = *absolute_root;
        if (!path_is_directory(temporary_root, filesystem_error)) {
            const std::string message = filesystem_error
                ? with_system_error("temporary root is invalid", filesystem_error)
                : "temporary root is not a directory";
            return make_result(
                GppCompilationStatus::invalid_configuration,
                message,
                filesystem_error);
        }
    } else {
        temporary_root = std::filesystem::temp_directory_path(filesystem_error);
        if (filesystem_error) {
            return make_result(
                GppCompilationStatus::filesystem_error,
                with_system_error(
                    "failed to prepare C++ compilation workspace",
                    filesystem_error),
                filesystem_error);
        }
    }

    auto workspace = create_workspace(temporary_root, filesystem_error);
    if (!workspace.has_value()) {
        return make_result(
            GppCompilationStatus::filesystem_error,
            with_system_error(
                "failed to prepare C++ compilation workspace", filesystem_error),
            filesystem_error);
    }

    const auto source_path = workspace->path() / "program.cpp";
    const auto executable_path = workspace->path() / "program";
    const auto stdout_path = workspace->path() / "compiler.stdout";
    const auto stderr_path = workspace->path() / "compiler.stderr";

    if (!write_all(source_path, generated_cpp, filesystem_error)) {
        return make_result(
            GppCompilationStatus::filesystem_error,
            with_system_error(
                "failed to prepare C++ compilation workspace", filesystem_error),
            filesystem_error);
    }

    auto stdout_descriptor = create_capture_file(stdout_path, filesystem_error);
    if (!stdout_descriptor.has_value()) {
        return make_result(
            GppCompilationStatus::filesystem_error,
            with_system_error(
                "failed to prepare C++ compilation workspace", filesystem_error),
            filesystem_error);
    }
    if (!move_away_from_standard_descriptors(
            *stdout_descriptor, filesystem_error)) {
        return make_result(
            GppCompilationStatus::filesystem_error,
            with_system_error(
                "failed to prepare C++ compilation workspace", filesystem_error),
            filesystem_error);
    }
    auto stderr_descriptor = create_capture_file(stderr_path, filesystem_error);
    if (!stderr_descriptor.has_value()) {
        return make_result(
            GppCompilationStatus::filesystem_error,
            with_system_error(
                "failed to prepare C++ compilation workspace", filesystem_error),
            filesystem_error);
    }
    if (!move_away_from_standard_descriptors(
            *stderr_descriptor, filesystem_error)) {
        return make_result(
            GppCompilationStatus::filesystem_error,
            with_system_error(
                "failed to prepare C++ compilation workspace", filesystem_error),
            filesystem_error);
    }

    const int null_descriptor = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
    if (null_descriptor < 0) {
        filesystem_error = errno_error();
        return make_result(
            GppCompilationStatus::filesystem_error,
            with_system_error(
                "failed to prepare C++ compilation workspace", filesystem_error),
            filesystem_error);
    }
    FileDescriptor standard_input(null_descriptor);
    if (!move_away_from_standard_descriptors(
            standard_input, filesystem_error)) {
        return make_result(
            GppCompilationStatus::filesystem_error,
            with_system_error(
                "failed to prepare C++ compilation workspace", filesystem_error),
            filesystem_error);
    }

    std::vector<std::string> arguments;
    arguments.reserve(10);
    arguments.push_back(config_.compiler_executable.string());
    arguments.emplace_back("-std=c++20");
    arguments.emplace_back("-Wall");
    arguments.emplace_back("-Wextra");
    arguments.emplace_back("-Wpedantic");
    arguments.emplace_back("-I");
    arguments.push_back(runtime_include->string());
    arguments.push_back(source_path.string());
    arguments.emplace_back("-o");
    arguments.push_back(executable_path.string());

    pid_t process_id = -1;
    const int spawn_error = spawn_compiler(
        config_.compiler_executable,
        arguments,
        standard_input.get(),
        stdout_descriptor->get(),
        stderr_descriptor->get(),
        process_id);
    if (spawn_error != 0) {
        const std::error_code launch_error(spawn_error, std::generic_category());
        return make_result(
            GppCompilationStatus::launch_failed,
            with_system_error(
                "C++ compiler could not be started", launch_error),
            launch_error);
    }

    // Only the child retains its duplicated standard descriptors from here.
    standard_input = FileDescriptor{};
    stdout_descriptor.reset();
    stderr_descriptor.reset();

    const WaitResult wait_result = wait_for_process(process_id, config_.timeout);

    GppCompilationResult result;
    if (!read_all(stdout_path, result.stdout_text, filesystem_error)
        || !read_all(stderr_path, result.stderr_text, filesystem_error)) {
        result.status = GppCompilationStatus::filesystem_error;
        result.system_error = filesystem_error;
        result.message = with_system_error(
            "failed to read C++ compiler output", filesystem_error);
        return result;
    }

    if (wait_result.timed_out) {
        result.status = GppCompilationStatus::timed_out;
        result.message = "C++ compilation timed out after "
            + std::to_string(config_.timeout.count()) + " ms";
        return result;
    }
    if (wait_result.wait_failed) {
        result.status = GppCompilationStatus::wait_failed;
        result.system_error = wait_result.system_error;
        result.message = with_system_error(
            "failed while waiting for C++ compiler", wait_result.system_error);
        return result;
    }
    if (WIFSIGNALED(wait_result.status)) {
        result.status = GppCompilationStatus::compiler_signaled;
        result.signal = WTERMSIG(wait_result.status);
        result.message = "C++ compiler terminated by signal "
            + std::to_string(*result.signal);
        return result;
    }
    if (!WIFEXITED(wait_result.status)) {
        const auto state_error = std::make_error_code(std::errc::state_not_recoverable);
        result.status = GppCompilationStatus::wait_failed;
        result.system_error = state_error;
        result.message = with_system_error(
            "failed while waiting for C++ compiler", state_error);
        return result;
    }

    result.exit_code = WEXITSTATUS(wait_result.status);
    if (*result.exit_code != 0) {
        result.status = GppCompilationStatus::compilation_failed;
        result.message = "C++ compilation failed with exit code "
            + std::to_string(*result.exit_code);
        return result;
    }

    filesystem_error.clear();
    if (!is_executable_regular_file(executable_path, filesystem_error)) {
        result.status = GppCompilationStatus::filesystem_error;
        result.system_error = filesystem_error;
        result.message = filesystem_error
            ? with_system_error(
                "failed to validate C++ executable artifact", filesystem_error)
            : "C++ compiler did not produce an executable artifact";
        return result;
    }

    result.status = GppCompilationStatus::succeeded;
    auto owned_executable = executable_path;
    auto owned_workspace = workspace->release();
    result.program.emplace(CompiledProgram{
        std::move(owned_workspace),
        std::move(owned_executable),
    });
    return result;
#endif
}

}

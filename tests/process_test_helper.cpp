#include <csignal>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>

#if defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#endif

namespace {

std::filesystem::path output_path(const int argc, char* argv[])
{
    for (int index = 1; index + 1 < argc; ++index) {
        if (std::string_view{argv[index]} == "-o") {
            return argv[index + 1];
        }
    }
    return {};
}

std::filesystem::path source_path(const int argc, char* argv[])
{
    for (int index = 1; index + 2 < argc; ++index) {
        if (std::string_view{argv[index + 1]} == "-o") {
            return argv[index];
        }
    }
    return {};
}

std::string read_file(const std::filesystem::path& path)
{
    std::ifstream stream{path, std::ios::binary};
    if (!stream) {
        return {};
    }
    return std::string{
        std::istreambuf_iterator<char>{stream},
        std::istreambuf_iterator<char>{}};
}

bool has_expected_argv(
    const int argc,
    char* argv[],
    const std::filesystem::path& executable)
{
    if (argc != 10
        || std::string_view{argv[1]} != "-std=c++20"
        || std::string_view{argv[2]} != "-Wall"
        || std::string_view{argv[3]} != "-Wextra"
        || std::string_view{argv[4]} != "-Wpedantic"
        || std::string_view{argv[5]} != "-I"
        || std::string_view{argv[8]} != "-o") {
        return false;
    }

    const auto expected_include = read_file(
        executable.string() + ".expected-include");
    const std::filesystem::path source{argv[7]};
    const std::filesystem::path output{argv[9]};
    return std::string_view{argv[6]} == expected_include
        && source.filename() == "program.cpp"
        && output.filename() == "program"
        && source.parent_path() == output.parent_path();
}

int create_artifact(const std::filesystem::path& output)
{
    if (output.empty()) {
        std::cerr << "fake compiler did not receive -o\n";
        return 97;
    }

    {
        std::ofstream stream{output, std::ios::binary | std::ios::trunc};
        if (!stream) {
            std::cerr << "fake compiler could not create artifact\n";
            return 98;
        }
        stream << "fake executable artifact\n";
        if (!stream) {
            std::cerr << "fake compiler could not write artifact\n";
            return 99;
        }
    }

    std::error_code error;
    std::filesystem::permissions(
        output,
        std::filesystem::perms::owner_read
            | std::filesystem::perms::owner_write
            | std::filesystem::perms::owner_exec,
        std::filesystem::perm_options::replace,
        error);
    if (error) {
        std::cerr << "fake compiler could not mark artifact executable\n";
        return 100;
    }

    return 0;
}

#if defined(__unix__) || defined(__APPLE__)
void ignore_signal(const int signal_number)
{
    struct sigaction action {};
    action.sa_handler = SIG_IGN;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    static_cast<void>(sigaction(signal_number, &action, nullptr));
}

[[noreturn]] void wait_forever()
{
    for (;;) {
        static_cast<void>(pause());
    }
}

int run_timeout_tree(const std::filesystem::path& executable)
{
    ignore_signal(SIGTERM);

    const pid_t child = fork();
    if (child < 0) {
        return 101;
    }
    if (child == 0) {
        ignore_signal(SIGTERM);
        wait_forever();
    }

    std::ofstream pid_file{
        executable.string() + ".child.pid",
        std::ios::binary | std::ios::trunc};
    if (!pid_file) {
        static_cast<void>(kill(child, SIGKILL));
        return 102;
    }
    pid_file << child << '\n';
    pid_file.flush();
    if (!pid_file) {
        static_cast<void>(kill(child, SIGKILL));
        return 103;
    }

    wait_forever();
}
#endif

} // namespace

int main(const int argc, char* argv[])
{
    const std::filesystem::path executable = argc > 0
        ? std::filesystem::path{argv[0]}
        : std::filesystem::path{};
    const std::string mode = executable.filename().string();

    if (mode.find("fake-exit-23") != std::string::npos) {
        std::cout << "failure stdout\n";
        std::cerr << "failure stderr\n";
        return 23;
    }

#if defined(__unix__) || defined(__APPLE__)
    if (mode.find("fake-signal") != std::string::npos) {
        static_cast<void>(raise(SIGUSR1));
        return 104;
    }

    if (mode.find("fake-timeout-tree") != std::string::npos) {
        return run_timeout_tree(executable);
    }
#endif

    if (mode.find("fake-no-artifact") != std::string::npos) {
        return 0;
    }

    if (mode.find("fake-check-source") != std::string::npos) {
        const auto expected_path = executable.string() + ".expected-source";
        const auto input = source_path(argc, argv);
        if (input.empty() || read_file(input) != read_file(expected_path)) {
            std::cerr << "source bytes did not match\n";
            return 91;
        }
    }

    if (mode.find("fake-check-argv") != std::string::npos) {
        if (!has_expected_argv(argc, argv, executable)
            || std::cin.peek() != std::char_traits<char>::eof()) {
            std::cerr << "compiler argv or stdin did not match contract\n";
            return 92;
        }
    }

    if (mode.find("fake-output") != std::string::npos) {
        std::cout << "compiler stdout\n";
        std::cerr << "compiler stderr\n";
    }

    if (mode.find("fake-large-output") != std::string::npos) {
        constexpr std::size_t output_size = 1024U * 1024U;
        const std::string stdout_chunk(4096, 'O');
        const std::string stderr_chunk(4096, 'E');
        for (std::size_t written = 0; written < output_size;
             written += stdout_chunk.size()) {
            std::cout.write(
                stdout_chunk.data(),
                static_cast<std::streamsize>(stdout_chunk.size()));
            std::cerr.write(
                stderr_chunk.data(),
                static_cast<std::streamsize>(stderr_chunk.size()));
        }
    }

    return create_artifact(output_path(argc, argv));
}

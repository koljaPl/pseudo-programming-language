#pragma once

#include "pseudo/common/source_span.hpp"

#include <cstddef>
#include <deque>
#include <filesystem>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace tpp {

// Positions are one-based values intended for user-facing diagnostics.
struct SourcePosition {
    std::size_t line;
    std::size_t column;

    constexpr bool operator==(const SourcePosition& other) const noexcept {
        return line == other.line && column == other.column;
    }
};

struct SourceLoadError {
    std::filesystem::path path;
    std::string message;
};

using SourceLoadResult = std::variant<SourceId, SourceLoadError>;

class SourceManager {
public:
    [[nodiscard]] SourceId add_source(
        std::string display_name,
        std::string contents);

    [[nodiscard]] SourceLoadResult load_file(
        const std::filesystem::path& path);

    [[nodiscard]] std::string_view contents(SourceId source) const;
    [[nodiscard]] std::string_view slice(SourceSpan span) const;
    [[nodiscard]] std::string_view display_name(SourceId source) const;
    [[nodiscard]] SourcePosition position(SourceLocation location) const;
    [[nodiscard]] std::string_view line_containing(
        SourceLocation location) const;

private:
    struct SourceFile {
        std::string display_name;
        std::string contents;
        std::vector<std::size_t> line_starts;
    };

    [[nodiscard]] const SourceFile& source_file(SourceId source) const;
    [[nodiscard]] std::size_t line_index(
        const SourceFile& source,
        std::size_t offset) const;

    std::deque<SourceFile> sources_;
};

}

#include "pseudo/source/source_manager.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <utility>

namespace tpp {
namespace {

std::vector<std::size_t> find_line_starts(std::string_view contents) {
    std::vector<std::size_t> line_starts{0};

    for (std::size_t offset = 0; offset < contents.size(); ++offset) {
        if (contents[offset] == '\n') {
            line_starts.push_back(offset + 1);
        }
    }

    return line_starts;
}

std::string source_error_message(
    std::string_view action,
    const std::filesystem::path& path) {
    return std::string{action} + " '" + path.string() + "'";
}

}

SourceId SourceManager::add_source(
    std::string display_name,
    std::string contents) {
    const SourceId source{sources_.size()};
    auto line_starts = find_line_starts(contents);

    sources_.push_back(SourceFile{
        .display_name = std::move(display_name),
        .contents = std::move(contents),
        .line_starts = std::move(line_starts),
    });

    return source;
}

SourceLoadResult SourceManager::load_file(
    const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary};
    if (!input) {
        return SourceLoadError{
            .path = path,
            .message = source_error_message("cannot open", path),
        };
    }

    std::string contents{
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{},
    };

    if (input.bad()) {
        return SourceLoadError{
            .path = path,
            .message = source_error_message("cannot read", path),
        };
    }

    return add_source(path.string(), std::move(contents));
}

std::string_view SourceManager::contents(SourceId source) const {
    return source_file(source).contents;
}

std::string_view SourceManager::slice(SourceSpan span) const {
    const auto& source = source_file(span.source);

    if (span.begin > span.end || span.end > source.contents.size()) {
        throw std::out_of_range{"source span is outside the source buffer"};
    }

    return std::string_view{source.contents}.substr(
        span.begin,
        span.end - span.begin);
}

std::string_view SourceManager::display_name(SourceId source) const {
    return source_file(source).display_name;
}

SourcePosition SourceManager::position(SourceLocation location) const {
    const auto& source = source_file(location.source);
    const auto index = line_index(source, location.offset);
    const auto line_start = source.line_starts[index];

    return SourcePosition{
        .line = index + 1,
        .column = location.offset - line_start + 1,
    };
}

std::string_view SourceManager::line_containing(
    SourceLocation location) const {
    const auto& source = source_file(location.source);
    const auto index = line_index(source, location.offset);
    const auto begin = source.line_starts[index];

    auto end = source.contents.find('\n', begin);
    if (end == std::string::npos) {
        end = source.contents.size();
    }

    if (end > begin && source.contents[end - 1] == '\r') {
        --end;
    }

    return std::string_view{source.contents}.substr(begin, end - begin);
}

const SourceManager::SourceFile& SourceManager::source_file(
    SourceId source) const {
    if (source.value >= sources_.size()) {
        throw std::out_of_range{"unknown source id"};
    }

    return sources_[source.value];
}

std::size_t SourceManager::line_index(
    const SourceFile& source,
    std::size_t offset) const {
    if (offset > source.contents.size()) {
        throw std::out_of_range{"source offset is outside the source buffer"};
    }

    const auto next_line = std::upper_bound(
        source.line_starts.begin(),
        source.line_starts.end(),
        offset);

    return static_cast<std::size_t>(
        std::distance(source.line_starts.begin(), next_line) - 1);
}

}

#pragma once

#include <cstddef>

namespace tpp {

struct SourceId {
    std::size_t value;

    constexpr bool operator==(const SourceId& other) const noexcept {
        return value == other.value;
    }
};

// Source offsets are zero-based bytes; spans use the half-open range [begin, end).
struct SourceLocation {
    SourceId source;
    std::size_t offset;
};

struct SourceSpan {
    SourceId source;
    std::size_t begin;
    std::size_t end;

    [[nodiscard]] constexpr bool empty() const noexcept {
        return begin == end;
    }

    [[nodiscard]] constexpr std::size_t length() const noexcept {
        return end - begin;
    }
};

}

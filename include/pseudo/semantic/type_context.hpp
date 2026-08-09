#pragma once

#include <cstddef>
#include <optional>
#include <variant>
#include <vector>

namespace tpp {

// Type IDs are stable within one context. Rebuilding the context invalidates
// every previously obtained non-primitive ID.
struct TypeId {
    std::size_t value;

    constexpr bool operator==(const TypeId& other) const noexcept {
        return value == other.value;
    }
};

enum class PrimitiveTypeKind {
    integer,
    boolean,
    character,
    string,
    void_type,
};

struct SemanticVectorType {
    TypeId element_type;

    constexpr bool operator==(
        const SemanticVectorType& other) const noexcept {
        return element_type == other.element_type;
    }
};

using SemanticType = std::variant<PrimitiveTypeKind, SemanticVectorType>;

class TypeContext {
public:
    TypeContext();

    [[nodiscard]] TypeId integer_type() const noexcept;
    [[nodiscard]] TypeId boolean_type() const noexcept;
    [[nodiscard]] TypeId character_type() const noexcept;
    [[nodiscard]] TypeId string_type() const noexcept;
    [[nodiscard]] TypeId void_type() const noexcept;

    [[nodiscard]] std::optional<TypeId> vector_type(TypeId element_type);
    [[nodiscard]] std::optional<SemanticType> lookup(TypeId type) const noexcept;
    [[nodiscard]] std::size_t type_count() const noexcept;

private:
    std::vector<SemanticType> types_;
};

}

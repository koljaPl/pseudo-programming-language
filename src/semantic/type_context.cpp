#include "pseudo/semantic/type_context.hpp"

#include <cstddef>

namespace tpp {
namespace {

constexpr TypeId integer_type_id{0};
constexpr TypeId boolean_type_id{1};
constexpr TypeId character_type_id{2};
constexpr TypeId string_type_id{3};
constexpr TypeId void_type_id{4};

}

TypeContext::TypeContext()
    : types_{
          PrimitiveTypeKind::integer,
          PrimitiveTypeKind::boolean,
          PrimitiveTypeKind::character,
          PrimitiveTypeKind::string,
          PrimitiveTypeKind::void_type,
      } {
}

TypeId TypeContext::integer_type() const noexcept {
    return integer_type_id;
}

TypeId TypeContext::boolean_type() const noexcept {
    return boolean_type_id;
}

TypeId TypeContext::character_type() const noexcept {
    return character_type_id;
}

TypeId TypeContext::string_type() const noexcept {
    return string_type_id;
}

TypeId TypeContext::void_type() const noexcept {
    return void_type_id;
}

std::optional<TypeId> TypeContext::vector_type(const TypeId element_type) {
    const auto element = lookup(element_type);
    if (!element.has_value()
        || (std::holds_alternative<PrimitiveTypeKind>(*element)
            && std::get<PrimitiveTypeKind>(*element)
                == PrimitiveTypeKind::void_type)) {
        return std::nullopt;
    }

    for (std::size_t index = 0; index < types_.size(); ++index) {
        const auto* vector = std::get_if<SemanticVectorType>(&types_[index]);
        if (vector != nullptr && vector->element_type == element_type) {
            return TypeId{index};
        }
    }

    const TypeId type{types_.size()};
    types_.push_back(SemanticVectorType{element_type});
    return type;
}

std::optional<SemanticType> TypeContext::lookup(const TypeId type) const noexcept {
    if (type.value >= types_.size()) {
        return std::nullopt;
    }

    return types_[type.value];
}

std::size_t TypeContext::type_count() const noexcept {
    return types_.size();
}

}

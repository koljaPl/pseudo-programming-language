#pragma once

#include "pseudo/common/source_span.hpp"

#include <memory>
#include <variant>

namespace tpp {

enum class ScalarTypeKind {
    integer,
    boolean,
    character,
    string,
};

struct ValueType;

struct VectorType {
    std::unique_ptr<ValueType> element_type;
};

using ValueTypeNode = std::variant<ScalarTypeKind, VectorType>;

struct ValueType {
    SourceSpan span;
    ValueTypeNode node;
};

struct VoidType {};

using ReturnTypeNode = std::variant<VoidType, ValueType>;

struct ReturnType {
    SourceSpan span;
    ReturnTypeNode node;
};

}

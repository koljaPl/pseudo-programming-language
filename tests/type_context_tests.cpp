#include "test_support.hpp"

#include "pseudo/ast/type.hpp"
#include "pseudo/semantic/ast_type.hpp"
#include "pseudo/semantic/type_context.hpp"

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <utility>
#include <variant>

namespace {

using tpp::PrimitiveTypeKind;
using tpp::ReturnType;
using tpp::ScalarTypeKind;
using tpp::SemanticType;
using tpp::SemanticVectorType;
using tpp::SourceId;
using tpp::SourceSpan;
using tpp::TypeContext;
using tpp::TypeId;
using tpp::ValueType;
using tpp::VectorType;
using tpp::VoidType;
using tpp::type_id_for;

SourceSpan span(
    const std::size_t begin = 0,
    const std::size_t end = 1,
    const std::size_t source = 0)
{
    return SourceSpan{
        .source = SourceId{source},
        .begin = begin,
        .end = end,
    };
}

ValueType scalar_type(
    const ScalarTypeKind kind,
    const SourceSpan source_span = span())
{
    return ValueType{
        .span = source_span,
        .node = kind,
    };
}

ValueType vector_type(
    ValueType element_type,
    const SourceSpan source_span = span())
{
    return ValueType{
        .span = source_span,
        .node = VectorType{
            .element_type =
                std::make_unique<ValueType>(std::move(element_type)),
        },
    };
}

PrimitiveTypeKind require_primitive(
    const std::optional<SemanticType>& type)
{
    TPP_CHECK(type.has_value());
    const auto* primitive = std::get_if<PrimitiveTypeKind>(&*type);
    TPP_CHECK(primitive != nullptr);
    return *primitive;
}

SemanticVectorType require_vector(
    const std::optional<SemanticType>& type)
{
    TPP_CHECK(type.has_value());
    const auto* vector = std::get_if<SemanticVectorType>(&*type);
    TPP_CHECK(vector != nullptr);
    return *vector;
}

TypeId require_type_id(const std::optional<TypeId>& type)
{
    TPP_CHECK(type.has_value());
    return *type;
}

void primitive_ids_are_canonical()
{
    TypeContext first;
    TypeContext second;

    TPP_CHECK_EQ(first.integer_type(), TypeId{0});
    TPP_CHECK_EQ(first.boolean_type(), TypeId{1});
    TPP_CHECK_EQ(first.character_type(), TypeId{2});
    TPP_CHECK_EQ(first.string_type(), TypeId{3});
    TPP_CHECK_EQ(first.void_type(), TypeId{4});
    TPP_CHECK_EQ(first.type_count(), std::size_t{5});

    TPP_CHECK_EQ(first.integer_type(), second.integer_type());
    TPP_CHECK_EQ(first.boolean_type(), second.boolean_type());
    TPP_CHECK_EQ(first.character_type(), second.character_type());
    TPP_CHECK_EQ(first.string_type(), second.string_type());
    TPP_CHECK_EQ(first.void_type(), second.void_type());
}

void primitive_ids_are_distinct()
{
    TypeContext types;
    const std::array primitives{
        types.integer_type(),
        types.boolean_type(),
        types.character_type(),
        types.string_type(),
        types.void_type(),
    };

    for (std::size_t left = 0; left < primitives.size(); ++left) {
        for (std::size_t right = left + 1; right < primitives.size(); ++right) {
            TPP_CHECK(primitives[left] != primitives[right]);
        }
    }
}

void primitive_descriptors_match_their_ids()
{
    TypeContext types;

    TPP_CHECK_EQ(
        require_primitive(types.lookup(types.integer_type())),
        PrimitiveTypeKind::integer);
    TPP_CHECK_EQ(
        require_primitive(types.lookup(types.boolean_type())),
        PrimitiveTypeKind::boolean);
    TPP_CHECK_EQ(
        require_primitive(types.lookup(types.character_type())),
        PrimitiveTypeKind::character);
    TPP_CHECK_EQ(
        require_primitive(types.lookup(types.string_type())),
        PrimitiveTypeKind::string);
    TPP_CHECK_EQ(
        require_primitive(types.lookup(types.void_type())),
        PrimitiveTypeKind::void_type);
}

void invalid_lookup_returns_no_type()
{
    const TypeContext types;

    TPP_CHECK(!types.lookup(TypeId{types.type_count()}).has_value());
    TPP_CHECK(!types.lookup(TypeId{999}).has_value());
}

void vector_types_are_interned()
{
    TypeContext types;

    const auto first = require_type_id(types.vector_type(types.integer_type()));
    const auto second = require_type_id(types.vector_type(types.integer_type()));

    TPP_CHECK_EQ(first, second);
    TPP_CHECK_EQ(types.type_count(), std::size_t{6});
    TPP_CHECK_EQ(
        require_vector(types.lookup(first)).element_type,
        types.integer_type());
}

void vectors_preserve_element_type_and_nesting()
{
    TypeContext types;
    const auto integers =
        require_type_id(types.vector_type(types.integer_type()));
    const auto booleans =
        require_type_id(types.vector_type(types.boolean_type()));
    const auto nested = require_type_id(types.vector_type(integers));

    TPP_CHECK(integers != booleans);
    TPP_CHECK(integers != nested);
    TPP_CHECK(booleans != nested);
    TPP_CHECK_EQ(
        require_vector(types.lookup(booleans)).element_type,
        types.boolean_type());
    TPP_CHECK_EQ(
        require_vector(types.lookup(nested)).element_type,
        integers);
    TPP_CHECK_EQ(
        require_type_id(types.vector_type(integers)),
        nested);
}

void invalid_and_void_elements_do_not_mutate_the_context()
{
    TypeContext types;
    const auto initial_count = types.type_count();

    TPP_CHECK(!types.vector_type(types.void_type()).has_value());
    TPP_CHECK_EQ(types.type_count(), initial_count);

    TPP_CHECK(!types.vector_type(TypeId{initial_count + 20}).has_value());
    TPP_CHECK_EQ(types.type_count(), initial_count);
}

void type_ids_remain_stable_as_the_context_grows()
{
    TypeContext types;
    const auto integers =
        require_type_id(types.vector_type(types.integer_type()));
    auto deepest = integers;
    auto previous = integers;

    for (std::size_t depth = 0; depth < 128; ++depth) {
        previous = deepest;
        deepest = require_type_id(types.vector_type(deepest));
    }

    TPP_CHECK_EQ(
        require_type_id(types.vector_type(types.integer_type())),
        integers);
    TPP_CHECK_EQ(
        require_vector(types.lookup(integers)).element_type,
        types.integer_type());
    TPP_CHECK_EQ(
        require_vector(types.lookup(deepest)).element_type,
        previous);
}

void lookup_returns_an_independent_value()
{
    TypeContext types;
    const auto integers =
        require_type_id(types.vector_type(types.integer_type()));
    auto descriptor = types.lookup(integers);
    TPP_CHECK(descriptor.has_value());

    auto deepest = integers;
    for (std::size_t depth = 0; depth < 64; ++depth) {
        deepest = require_type_id(types.vector_type(deepest));
    }

    TPP_CHECK_EQ(
        require_vector(descriptor).element_type,
        types.integer_type());
}

void scalar_ast_types_map_to_primitives()
{
    TypeContext types;
    const std::array cases{
        std::pair{ScalarTypeKind::integer, types.integer_type()},
        std::pair{ScalarTypeKind::boolean, types.boolean_type()},
        std::pair{ScalarTypeKind::character, types.character_type()},
        std::pair{ScalarTypeKind::string, types.string_type()},
    };

    for (std::size_t index = 0; index < cases.size(); ++index) {
        const auto syntax_type = scalar_type(
            cases[index].first,
            span(index, index + 1, index + 10));
        TPP_CHECK_EQ(
            require_type_id(type_id_for(types, syntax_type)),
            cases[index].second);
    }

    const auto same_type_at_another_span =
        scalar_type(ScalarTypeKind::integer, span(40, 80, 7));
    TPP_CHECK_EQ(
        require_type_id(type_id_for(types, same_type_at_another_span)),
        types.integer_type());
}

void recursive_ast_vector_types_are_interned()
{
    TypeContext types;
    const auto syntax_type = vector_type(
        vector_type(
            scalar_type(ScalarTypeKind::string, span(14, 20, 3)),
            span(7, 21, 3)),
        span(0, 22, 3));

    const auto semantic_type = require_type_id(type_id_for(types, syntax_type));
    const auto inner = require_vector(types.lookup(semantic_type)).element_type;

    TPP_CHECK_EQ(
        require_vector(types.lookup(inner)).element_type,
        types.string_type());
    TPP_CHECK_EQ(types.type_count(), std::size_t{7});
    TPP_CHECK_EQ(
        require_type_id(type_id_for(types, syntax_type)),
        semantic_type);

    const auto* outer_syntax = std::get_if<VectorType>(&syntax_type.node);
    TPP_CHECK(outer_syntax != nullptr);
    TPP_CHECK(outer_syntax->element_type != nullptr);
    TPP_CHECK_EQ(syntax_type.span.source.value, std::size_t{3});
    TPP_CHECK_EQ(syntax_type.span.begin, std::size_t{0});
    TPP_CHECK_EQ(syntax_type.span.end, std::size_t{22});
}

void return_ast_types_map_to_semantic_types()
{
    TypeContext types;
    const ReturnType void_return{
        .span = span(0, 4),
        .node = VoidType{},
    };
    ReturnType vector_return{
        .span = span(0, 11),
        .node = vector_type(
            scalar_type(ScalarTypeKind::character, span(7, 11)),
            span(0, 11)),
    };

    TPP_CHECK_EQ(
        require_type_id(type_id_for(types, void_return)),
        types.void_type());

    const auto semantic_vector =
        require_type_id(type_id_for(types, vector_return));
    TPP_CHECK_EQ(
        require_vector(types.lookup(semantic_vector)).element_type,
        types.character_type());
}

void malformed_ast_vector_returns_no_type()
{
    TypeContext types;
    const ValueType malformed{
        .span = span(0, 8),
        .node = VectorType{.element_type = nullptr},
    };
    const auto initial_count = types.type_count();

    TPP_CHECK(!type_id_for(types, malformed).has_value());
    TPP_CHECK_EQ(types.type_count(), initial_count);

    const ReturnType malformed_return{
        .span = span(0, 8),
        .node = ValueType{
            .span = span(0, 8),
            .node = VectorType{.element_type = nullptr},
        },
    };
    TPP_CHECK(!type_id_for(types, malformed_return).has_value());
    TPP_CHECK_EQ(types.type_count(), initial_count);
}

void invalid_scalar_ast_returns_no_type()
{
    TypeContext types;
    const ValueType malformed{
        .span = span(),
        .node = static_cast<ScalarTypeKind>(100),
    };

    TPP_CHECK(!type_id_for(types, malformed).has_value());
    TPP_CHECK_EQ(types.type_count(), std::size_t{5});
}

}

int main()
{
    return tpp::test::run({
        {"primitive IDs are canonical", primitive_ids_are_canonical},
        {"primitive IDs are distinct", primitive_ids_are_distinct},
        {"primitive descriptors match their IDs", primitive_descriptors_match_their_ids},
        {"invalid lookup returns no type", invalid_lookup_returns_no_type},
        {"vector types are interned", vector_types_are_interned},
        {"vectors preserve element type and nesting", vectors_preserve_element_type_and_nesting},
        {"invalid and void elements do not mutate the context",
         invalid_and_void_elements_do_not_mutate_the_context},
        {"type IDs remain stable as the context grows",
         type_ids_remain_stable_as_the_context_grows},
        {"lookup returns an independent value", lookup_returns_an_independent_value},
        {"scalar AST types map to primitives", scalar_ast_types_map_to_primitives},
        {"recursive AST vector types are interned", recursive_ast_vector_types_are_interned},
        {"return AST types map to semantic types", return_ast_types_map_to_semantic_types},
        {"malformed AST vector returns no type", malformed_ast_vector_returns_no_type},
        {"invalid scalar AST returns no type", invalid_scalar_ast_returns_no_type},
    });
}

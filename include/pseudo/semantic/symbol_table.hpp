#pragma once

#include "pseudo/common/source_span.hpp"
#include "pseudo/semantic/type_context.hpp"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace tpp {

struct SymbolId {
    std::size_t value;

    constexpr bool operator==(const SymbolId& other) const noexcept {
        return value == other.value;
    }
};

struct ScopeId {
    std::size_t value;

    constexpr bool operator==(const ScopeId& other) const noexcept {
        return value == other.value;
    }
};

struct VariableSymbol {
    // A for-each binding has no known type until type checking examines its
    // iterable. Every explicitly typed variable and for-range binding stores
    // a concrete TypeId.
    std::optional<TypeId> type;
};

struct ParameterSymbol {
    TypeId type;
};

struct FunctionSymbol {
    TypeId return_type;
    std::vector<TypeId> parameter_types;
};

using SymbolData = std::variant<
    VariableSymbol,
    ParameterSymbol,
    FunctionSymbol>;

struct Symbol {
    std::string name;
    SourceSpan declaration_span;
    SymbolData data;
};

struct DuplicateSymbol {
    SymbolId existing_symbol;
};

using SymbolInsertResult = std::variant<SymbolId, DuplicateSymbol>;

// IDs remain stable while the table grows. A reference returned by scope() may
// be invalidated by create_child_scope(); a symbol() reference by insert().
class Scope {
public:
    [[nodiscard]] std::optional<ScopeId> parent() const noexcept;

private:
    friend class SymbolTable;

    struct StringHash {
        using is_transparent = void;

        [[nodiscard]] std::size_t operator()(
            std::string_view value) const noexcept {
            return std::hash<std::string_view>{}(value);
        }
    };

    using Bindings = std::unordered_map<
        std::string,
        SymbolId,
        StringHash,
        std::equal_to<>>;

    explicit Scope(std::optional<ScopeId> parent);

    std::optional<ScopeId> parent_;
    Bindings bindings_;
};

class SymbolTable {
public:
    SymbolTable();

    [[nodiscard]] ScopeId global_scope() const noexcept;
    [[nodiscard]] ScopeId create_child_scope(ScopeId parent);

    [[nodiscard]] SymbolInsertResult insert(ScopeId scope, Symbol symbol);
    [[nodiscard]] std::optional<SymbolId> lookup_local(
        ScopeId scope,
        std::string_view name) const;
    [[nodiscard]] std::optional<SymbolId> lookup(
        ScopeId scope,
        std::string_view name) const;

    [[nodiscard]] const Scope& scope(ScopeId id) const;
    [[nodiscard]] const Symbol& symbol(SymbolId id) const;

    [[nodiscard]] std::size_t symbol_count() const noexcept;
    [[nodiscard]] std::size_t scope_count() const noexcept;

private:
    [[nodiscard]] Scope& mutable_scope(ScopeId id);

    std::vector<Symbol> symbols_;
    std::vector<Scope> scopes_;
};

}

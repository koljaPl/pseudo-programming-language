#include "pseudo/semantic/symbol_table.hpp"

#include <stdexcept>
#include <utility>

namespace tpp {

Scope::Scope(std::optional<ScopeId> parent)
    : parent_{parent} {}

std::optional<ScopeId> Scope::parent() const noexcept {
    return parent_;
}

SymbolTable::SymbolTable() {
    scopes_.push_back(Scope{std::nullopt});
}

ScopeId SymbolTable::global_scope() const noexcept {
    return ScopeId{0};
}

ScopeId SymbolTable::create_child_scope(ScopeId parent) {
    static_cast<void>(scope(parent));

    const ScopeId child{scopes_.size()};
    scopes_.push_back(Scope{parent});
    return child;
}

SymbolInsertResult SymbolTable::insert(ScopeId scope_id, Symbol symbol) {
    auto& target = mutable_scope(scope_id);
    const SymbolId id{symbols_.size()};
    const auto [binding, inserted] = target.bindings_.emplace(symbol.name, id);
    if (!inserted) {
        return DuplicateSymbol{binding->second};
    }

    try {
        symbols_.push_back(std::move(symbol));
    } catch (...) {
        target.bindings_.erase(binding);
        throw;
    }

    return id;
}

std::optional<SymbolId> SymbolTable::lookup_local(
    ScopeId scope_id,
    std::string_view name) const {
    const auto& current = scope(scope_id);
    const auto symbol = current.bindings_.find(name);

    if (symbol == current.bindings_.end()) {
        return std::nullopt;
    }

    return symbol->second;
}

std::optional<SymbolId> SymbolTable::lookup(
    ScopeId scope_id,
    std::string_view name) const {
    auto current_id = std::optional<ScopeId>{scope_id};

    while (current_id.has_value()) {
        const auto& current = scope(*current_id);
        const auto found = current.bindings_.find(name);
        if (found != current.bindings_.end()) {
            return found->second;
        }

        current_id = current.parent();
    }

    return std::nullopt;
}

const Scope& SymbolTable::scope(ScopeId id) const {
    if (id.value >= scopes_.size()) {
        throw std::out_of_range{"unknown scope id"};
    }

    return scopes_[id.value];
}

const Symbol& SymbolTable::symbol(SymbolId id) const {
    if (id.value >= symbols_.size()) {
        throw std::out_of_range{"unknown symbol id"};
    }

    return symbols_[id.value];
}

std::size_t SymbolTable::symbol_count() const noexcept {
    return symbols_.size();
}

std::size_t SymbolTable::scope_count() const noexcept {
    return scopes_.size();
}

Scope& SymbolTable::mutable_scope(ScopeId id) {
    if (id.value >= scopes_.size()) {
        throw std::out_of_range{"unknown scope id"};
    }

    return scopes_[id.value];
}

}

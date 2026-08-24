#include "pseudo/semantic/resolution_info.hpp"

#include <utility>

namespace tpp {
namespace {

template <typename Node>
std::optional<ResolutionTarget> find_resolution(
    const std::unordered_map<const Node*, ResolutionTarget>& resolutions,
    const Node& node) {
    const auto found = resolutions.find(&node);
    if (found == resolutions.end()) {
        return std::nullopt;
    }

    return found->second;
}

}

std::optional<ResolutionTarget> ResolutionInfo::resolution_for(
    const IdentifierExpression& reference) const {
    return find_resolution(identifiers_, reference);
}

std::optional<ResolutionTarget> ResolutionInfo::resolution_for(
    const AssignmentTarget& reference) const {
    return find_resolution(assignment_targets_, reference);
}

bool ResolutionInfo::empty() const noexcept {
    return identifiers_.empty() && assignment_targets_.empty();
}

void ResolutionInfo::record(
    const IdentifierExpression& reference,
    ResolutionTarget target) {
    identifiers_.emplace(&reference, std::move(target));
}

void ResolutionInfo::record(
    const AssignmentTarget& reference,
    ResolutionTarget target) {
    assignment_targets_.emplace(&reference, std::move(target));
}

}

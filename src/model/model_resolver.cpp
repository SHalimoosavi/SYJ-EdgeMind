#include "model/model_resolver.h"

#include <vector>

#include "model/model_registry.h"

namespace syj::edgemind {

ModelResolutionResult resolve_model_path(const std::string& model_path, const std::string& model_id,
                                          const std::string& registry_path) {
    ModelResolutionResult result;

    const bool has_path = !model_path.empty();
    const bool has_id = !model_id.empty();

    if (!has_path && !has_id) {
        result.status = ModelResolutionStatus::NeitherProvided;
        return result;
    }
    if (has_path && has_id) {
        // No precedence rule exists anywhere in this codebase for this
        // case — see model_resolver.h's contract comment. Reject rather
        // than silently choosing one.
        result.status = ModelResolutionStatus::BothProvided;
        return result;
    }

    if (has_path) {
        // Direct-path flow, unchanged from every prior phase: the path is
        // returned as-is. Whether it's actually a valid/verifiable file is
        // entirely ModelVerifier's job, downstream, not this function's.
        result.status = ModelResolutionStatus::Resolved;
        result.resolved_path = model_path;
        return result;
    }

    // model_id flow: read-only registry lookup. ModelRegistry::load() is
    // used directly (rather than ModelRegistry::find_by_id()) so a
    // corrupted registry can be reported distinctly from "nothing
    // registered under this id" — both collapse to a single `false` return
    // from find_by_id(), which isn't granular enough for this contract.
    // This only reads the registry's already-parsed entries; it does not
    // know or duplicate anything about the registry's on-disk format.
    std::vector<RegistryEntry> entries;
    const RegistryLoadResult load_result = ModelRegistry::load(registry_path, &entries);

    if (load_result == RegistryLoadResult::Corrupted) {
        result.status = ModelResolutionStatus::RegistryUnreadable;
        return result;
    }

    // RegistryLoadResult::NotFound (no registry file yet) and Ok-but-no-match
    // both mean the same thing from this function's contract: nothing is
    // registered under that id yet, so there's nothing to resolve.
    for (const RegistryEntry& entry : entries) {
        if (entry.model_id == model_id) {
            result.status = ModelResolutionStatus::Resolved;
            result.resolved_path = entry.local_path;
            return result;
        }
    }

    result.status = ModelResolutionStatus::ModelIdNotFound;
    return result;
}

} // namespace syj::edgemind
